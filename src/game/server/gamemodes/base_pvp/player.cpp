#include <base/system.h>
#include <engine/shared/protocol.h>
#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/instagib/sql_stats.h>
#include <game/server/player.h>
#include <game/server/score.h>
#include <game/version.h>

void CPlayer::ResetStats()
{
	m_Kills = 0;
	m_Deaths = 0;
	m_Stats.Reset();
}

void CPlayer::WarmupAlert()
{
	// 0.7 has client side infinite warmup support
	// so we do only need the broadcast for 0.6 players
	if(Server()->IsSixup(GetCid()))
		return;

	m_SentWarmupAlerts++;
	if(m_SentWarmupAlerts < 3)
	{
		GameServer()->SendBroadcast("This is a warmup game. Call a restart vote to start.", GetCid());
	}
}

const char *CPlayer::GetTeamStr() const
{
	if(GetTeam() == TEAM_SPECTATORS)
		return "spectator";

	if(GameServer()->m_pController && !GameServer()->m_pController->IsTeamPlay())
		return "game";

	if(GetTeam() == TEAM_RED)
		return "red";
	return "blue";
}

void CPlayer::AddScore(int Score)
{
	if(GameServer()->m_pController && GameServer()->m_pController->IsWarmup())
	{
		WarmupAlert();
		return;
	}

	// never count score or win rounds in ddrace teams
	if(GameServer()->GetDDRaceTeam(GetCid()))
		return;

	// never decrement the tracked score
	// so fakers can not remove points from others
	if(Score > 0 && GameServer()->m_pController && GameServer()->m_pController->IsStatTrack())
		m_Stats.m_Points += Score;

	m_Score = m_Score.value_or(0) + Score;
}

void CPlayer::AddKills(int Amount)
{
	if(GameServer()->m_pController->IsStatTrack())
		m_Stats.m_Kills += Amount;

	m_Kills += Amount;
}

void CPlayer::AddDeaths(int Amount)
{
	if(GameServer()->m_pController->IsStatTrack())
		m_Stats.m_Deaths += Amount;

	m_Deaths += Amount;
}

void CPlayer::InstagibTick()
{
	if(m_StatsQueryResult != nullptr && m_StatsQueryResult->m_Completed)
	{
		ProcessStatsResult(*m_StatsQueryResult);
		m_StatsQueryResult = nullptr;
	}
	if(m_FastcapQueryResult != nullptr && m_FastcapQueryResult->m_Completed)
	{
		ProcessStatsResult(*m_FastcapQueryResult);
		m_FastcapQueryResult = nullptr;
	}
}

void CPlayer::ProcessStatsResult(CInstaSqlResult &Result)
{
	if(Result.m_Success) // SQL request was successful
	{
		switch(Result.m_MessageKind)
		{
		case EInstaSqlRequestType::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;
		case EInstaSqlRequestType::ALL:
		{
			bool PrimaryMessage = true;
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				if(GameServer()->ProcessSpamProtection(m_ClientId) && PrimaryMessage)
					break;

				GameServer()->SendChat(-1, TEAM_ALL, aMessage, -1);
				PrimaryMessage = false;
			}
			break;
		}
		case EInstaSqlRequestType::BROADCAST:
			if(Result.m_aBroadcast[0] != 0)
				GameServer()->SendBroadcast(Result.m_aBroadcast, -1);
			break;
		case EInstaSqlRequestType::CHAT_CMD_STATSALL:
			GameServer()->m_pController->OnShowStatsAll(&Result.m_Stats, this, Result.m_Info.m_aRequestedPlayer);
			break;
		case EInstaSqlRequestType::CHAT_CMD_RANK:
			GameServer()->m_pController->OnShowRank(Result.m_Rank, Result.m_RankedScore, Result.m_aRankColumnDisplay, this, Result.m_Info.m_aRequestedPlayer);
			break;
		case EInstaSqlRequestType::CHAT_CMD_MULTIS:
			GameServer()->m_pController->OnShowMultis(&Result.m_Stats, this, Result.m_Info.m_aRequestedPlayer);
			break;
		case EInstaSqlRequestType::PLAYER_DATA:
			GameServer()->m_pController->OnLoadedNameStats(&Result.m_Stats, this);
			break;
		}
	}
}

int64_t CPlayer::HandleMulti()
{
	int64_t TimeNow = time_timestamp();
	if((TimeNow - m_LastKillTime) > 5)
	{
		m_Multi = 1;
		return TimeNow;
	}

	if(!GameServer()->m_pController->IsStatTrack())
		return TimeNow;

	m_Multi++;
	if(m_Stats.m_BestMulti < m_Multi)
		m_Stats.m_BestMulti = m_Multi;
	int Index = m_Multi - 2;
	m_Stats.m_aMultis[Index > MAX_MULTIS ? MAX_MULTIS : Index]++;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "'%s' multi x%d!",
		Server()->ClientName(GetCid()), m_Multi);
	GameServer()->SendChat(-1, TEAM_ALL, aBuf);
	return TimeNow;
}

void CPlayer::SetTeamSpoofed(int Team, bool DoChatMsg)
{
	KillCharacter();

	m_Team = Team;
	m_LastSetTeam = Server()->Tick();
	m_LastActionTick = Server()->Tick();
	m_SpectatorId = SPEC_FREEVIEW;

	protocol7::CNetMsg_Sv_Team Msg;
	Msg.m_ClientId = m_ClientId;
	Msg.m_Team = GameServer()->m_pController->GetPlayerTeam(this, true); // might be a fake team
	Msg.m_Silent = !DoChatMsg;
	Msg.m_CooldownTick = m_LastSetTeam + Server()->TickSpeed() * g_Config.m_SvTeamChangeDelay;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);

	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(pPlayer && pPlayer->m_SpectatorId == m_ClientId)
				pPlayer->m_SpectatorId = SPEC_FREEVIEW;
		}
	}

	Server()->ExpireServerInfo();
}

void CPlayer::SetTeamNoKill(int Team, bool DoChatMsg)
{
	m_Team = Team;
	m_LastSetTeam = Server()->Tick();
	m_LastActionTick = Server()->Tick();
	m_SpectatorId = SPEC_FREEVIEW;

	// dead spec mode for 0.7
	if(!m_IsDead)
	{
		protocol7::CNetMsg_Sv_Team Msg;
		Msg.m_ClientId = m_ClientId;
		Msg.m_Team = m_Team;
		Msg.m_Silent = !DoChatMsg;
		Msg.m_CooldownTick = m_LastSetTeam + Server()->TickSpeed() * g_Config.m_SvTeamChangeDelay;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);
	}

	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(pPlayer && pPlayer->m_SpectatorId == m_ClientId)
				pPlayer->m_SpectatorId = SPEC_FREEVIEW;
		}
	}

	Server()->ExpireServerInfo();
}

void CPlayer::UpdateLastToucher(int ClientId)
{
	if(ClientId == GetCid())
		return;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		// covers the reset case when -1 is passed explicitly
		// to reset the last toucher when being hammered by a team mate in fng
		m_LastToucherId = -1;
		return;
	}

	// TODO: should we really reset the last toucher when we get shot by a team mate?
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(
		pPlayer &&
		GameServer()->m_pController &&
		GameServer()->m_pController->IsTeamplay() &&
		pPlayer->GetTeam() == GetTeam())
	{
		m_LastToucherId = -1;
		return;
	}

	m_LastToucherId = ClientId;
	m_TicksSinceLastTouch = 0;
}
