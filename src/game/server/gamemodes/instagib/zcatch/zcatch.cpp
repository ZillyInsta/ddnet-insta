#include <base/system.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <game/generated/protocol.h>
#include <game/generated/protocol7.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/entities/flag.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/server/score.h>
#include <game/version.h>

#include "zcatch.h"

CGameControllerZcatch::CGameControllerZcatch(class CGameContext *pGameServer) :
	CGameControllerInstagib(pGameServer)
{
	m_GameFlags = 0;
	m_AllowSkinChange = false;
	m_pGameType = "zCatch";
	m_DefaultWeapon = GetDefaultWeaponBasedOnSpawnWeapons();

	for(auto &Color : m_aBodyColors)
		Color = 0;

	m_pStatsTable = "";
	if(m_SpawnWeapons == ESpawnWeapons::SPAWN_WEAPON_GRENADE)
		m_pStatsTable = "zcatch_grenade";
	else if(m_SpawnWeapons == ESpawnWeapons::SPAWN_WEAPON_LASER)
		m_pStatsTable = "zcatch_laser";
	if(m_pStatsTable[0])
	{
		m_pExtraColumns = new CZCatchColumns();
		m_pSqlStats->SetExtraColumns(m_pExtraColumns);
		m_pSqlStats->CreateTable(m_pStatsTable);
	}
}

void CGameControllerZcatch::OnShowStatsAll(const CSqlStatsPlayer *pStats, class CPlayer *pRequestingPlayer, const char *pRequestedName)
{
	CGameControllerInstagib::OnShowStatsAll(pStats, pRequestingPlayer, pRequestedName);

	pStats->Dump(m_pExtraColumns);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "~ Seconds in game: %d", pStats->m_TicksInGame / Server()->TickSpeed());
	GameServer()->SendChatTarget(pRequestingPlayer->GetCid(), aBuf);

	str_format(aBuf, sizeof(aBuf), "~ Seconds caught: %d", pStats->m_TicksCaught / Server()->TickSpeed());
	GameServer()->SendChatTarget(pRequestingPlayer->GetCid(), aBuf);
}

void CGameControllerZcatch::OnShowRoundStats(const CSqlStatsPlayer *pStats, class CPlayer *pRequestingPlayer, const char *pRequestedName)
{
	CGameControllerInstagib::OnShowRoundStats(pStats, pRequestingPlayer, pRequestedName);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "~ Seconds in game: %d", pStats->m_TicksInGame / Server()->TickSpeed());
	GameServer()->SendChatTarget(pRequestingPlayer->GetCid(), aBuf);

	str_format(aBuf, sizeof(aBuf), "~ Seconds caught: %d", pStats->m_TicksCaught / Server()->TickSpeed());
	GameServer()->SendChatTarget(pRequestingPlayer->GetCid(), aBuf);
}

CGameControllerZcatch::ECatchGameState CGameControllerZcatch::CatchGameState() const
{
	if(g_Config.m_SvReleaseGame)
		return ECatchGameState::RELEASE_GAME;
	return m_CatchGameState;
}

bool CGameControllerZcatch::IsCatchGameRunning() const
{
	return CatchGameState() == ECatchGameState::RUNNING ||
	       CatchGameState() == ECatchGameState::RUNNING_COMPETITIVE;
}

void CGameControllerZcatch::SetCatchGameState(ECatchGameState State)
{
	if(g_Config.m_SvReleaseGame)
	{
		m_CatchGameState = ECatchGameState::RELEASE_GAME;
		return;
	}
	m_CatchGameState = State;
}

bool CGameControllerZcatch::IsWinner(const CPlayer *pPlayer, char *pMessage, int SizeOfMessage)
{
	if(pMessage && SizeOfMessage)
		pMessage[0] = '\0';

	// you can only win on round end
	if(GameState() != IGS_END_ROUND)
		return false;
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		return false;
	if(pPlayer->m_IsDead)
		return false;
	// you can never win with 0 kills
	// this should cover edge cases where one spawns into the world
	// where everyone else is currently in a death screen
	if(!pPlayer->m_Spree)
		return false;
	// there are no winners in release games even if the round ends
	if(!IsCatchGameRunning())
		return false;
	// the win does not count in casual rounds
	if(CatchGameState() != ECatchGameState::RUNNING_COMPETITIVE)
	{
		if(pMessage)
			str_copy(pMessage, "The win did not count because the round was started with less than 10 players.", SizeOfMessage);
		return false;
	}

	if(pMessage)
		str_copy(pMessage, "+1 win was saved on your name (see /rank_wins).", SizeOfMessage);
	return !pPlayer->m_IsDead;
}

bool CGameControllerZcatch::IsLoser(const CPlayer *pPlayer)
{
	// because you can not win a casual game
	// it does also not track your loss
	if(CatchGameState() != ECatchGameState::RUNNING_COMPETITIVE)
		return false;

	// rage quit as dead player is counted as a loss
	// qutting mid game while being alive is not
	return pPlayer->m_IsDead;
}

int CGameControllerZcatch::PointsForWin(const CPlayer *pPlayer)
{
	int Points = pPlayer->m_vVictimIds.size() * 2;
	dbg_msg(
		"zcatch",
		"player '%s' earned %d points for winning with %d kills",
		Server()->ClientName(pPlayer->GetCid()),
		Points,
		pPlayer->m_vVictimIds.size());
	return Points;
}

void CGameControllerZcatch::OnRoundStart()
{
	CGameControllerInstagib::OnRoundStart();

	int ActivePlayers = NumActivePlayers();
	if(ActivePlayers < g_Config.m_SvZcatchMinPlayers && CatchGameState() != ECatchGameState::RELEASE_GAME)
	{
		SendChatTarget(-1, "Not enough players to start a round");
		SetCatchGameState(ECatchGameState::WAITING_FOR_PLAYERS);
	}

	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		pPlayer->m_GotRespawnInfo = false;
		pPlayer->m_vVictimIds.clear();
		pPlayer->m_KillerId = -1;

		// resets the winners color
		pPlayer->m_Spree = 0;
		pPlayer->m_UntrackedSpree = 0;
	}
}

CGameControllerZcatch::~CGameControllerZcatch() = default;

void CGameControllerZcatch::Tick()
{
	CGameControllerInstagib::Tick();

	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		// this is wasting a bit of clock cycles setting it every tick
		// it should be set on kill and then not be overwritten by info changes
		// but there is no git conflict free way of doing that
		SetCatchColors(pPlayer);

		if(m_aBodyColors[pPlayer->GetCid()] != pPlayer->m_TeeInfos.m_ColorBody)
		{
			m_aBodyColors[pPlayer->GetCid()] = pPlayer->m_TeeInfos.m_ColorBody;
			SendSkinBodyColor7(pPlayer->GetCid(), pPlayer->m_TeeInfos.m_ColorBody);
		}
	}
}

void CGameControllerZcatch::OnCharacterSpawn(class CCharacter *pChr)
{
	CGameControllerInstagib::OnCharacterSpawn(pChr);

	SetSpawnWeapons(pChr);
}

int CGameControllerZcatch::GetPlayerTeam(class CPlayer *pPlayer, bool Sixup)
{
	// spoof fake in game team
	// to get dead spec tees for 0.7 connections
	if(Sixup && pPlayer->m_IsDead)
		return TEAM_RED;

	return CGameControllerPvp::GetPlayerTeam(pPlayer, Sixup);
}

void CGameControllerZcatch::ReleasePlayer(class CPlayer *pPlayer, const char *pMsg)
{
	GameServer()->SendChatTarget(pPlayer->GetCid(), pMsg);

	UpdateCatchTicks(pPlayer);
	pPlayer->m_IsDead = false;
	pPlayer->m_KillerId = -1;

	if(pPlayer->m_WantsToJoinSpectators)
	{
		pPlayer->SetTeam(TEAM_SPECTATORS);
		pPlayer->m_WantsToJoinSpectators = false;
	}
	else
		pPlayer->SetTeamNoKill(TEAM_RED);
}

bool CGameControllerZcatch::OnSelfkill(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;
	if(pPlayer->m_vVictimIds.empty())
		return false;

	CPlayer *pVictim = nullptr;
	while(!pVictim)
	{
		if(pPlayer->m_vVictimIds.empty())
			return false;

		int ReleaseId = pPlayer->m_vVictimIds.back();
		pPlayer->m_vVictimIds.pop_back();

		pVictim = GameServer()->m_apPlayers[ReleaseId];
	}
	if(!pVictim)
		return false;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "You were released by '%s'", Server()->ClientName(pPlayer->GetCid()));
	ReleasePlayer(pVictim, aBuf);

	str_format(aBuf, sizeof(aBuf), "You released '%s' (%d players left)", Server()->ClientName(pVictim->GetCid()), pPlayer->m_vVictimIds.size());
	SendChatTarget(ClientId, aBuf);

	return true;
}

void CGameControllerZcatch::KillPlayer(class CPlayer *pVictim, class CPlayer *pKiller)
{
	if(!pKiller)
		return;
	if(!pKiller->GetCharacter())
		return;
	if(pKiller->GetTeam() == TEAM_SPECTATORS)
		return;
	if(pKiller->m_IsDead)
	{
		dbg_msg(
			"zcatch",
			"warning '%s' was killed by the dead (but not spec) player '%s'",
			Server()->ClientName(pVictim->GetCid()),
			Server()->ClientName(pKiller->GetCid()));
		return;
	}

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "You are spectator until '%s' dies", Server()->ClientName(pKiller->GetCid()));
	GameServer()->SendChatTarget(pVictim->GetCid(), aBuf);

	UpdateCatchTicks(pVictim);
	pVictim->m_IsDead = true;
	pVictim->m_KillerId = pKiller->GetCid();
	if(pVictim->GetTeam() != TEAM_SPECTATORS)
		pVictim->SetTeamNoKill(TEAM_SPECTATORS);
	pVictim->m_SpectatorId = pKiller->GetCid();

	int Found = count(pKiller->m_vVictimIds.begin(), pKiller->m_vVictimIds.end(), pVictim->GetCid());
	if(!Found)
		pKiller->m_vVictimIds.emplace_back(pVictim->GetCid());
}

void CGameControllerZcatch::OnCaught(class CPlayer *pVictim, class CPlayer *pKiller)
{
	if(pVictim->GetCid() == pKiller->GetCid())
		return;

	if(CatchGameState() == ECatchGameState::WAITING_FOR_PLAYERS)
	{
		if(!pKiller->m_GotRespawnInfo)
			GameServer()->SendChatTarget(pKiller->GetCid(), "Kill respawned because there are not enough players.");
		pKiller->m_GotRespawnInfo = true;
		return;
	}
	if(CatchGameState() == ECatchGameState::RELEASE_GAME)
	{
		if(!pKiller->m_GotRespawnInfo)
			GameServer()->SendChatTarget(pKiller->GetCid(), "Kill respawned because this is a release game.");
		pKiller->m_GotRespawnInfo = true;
		return;
	}

	KillPlayer(pVictim, pKiller);
}

int CGameControllerZcatch::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int WeaponId)
{
	CGameControllerInstagib::OnCharacterDeath(pVictim, pKiller, WeaponId);

	// TODO: revisit this edge case when zcatch is done
	//       a killer leaving while the bullet is flying
	if(!pKiller)
		return 0;

	// do not release players when the winner leaves during round end
	if(GameState() == IGS_END_ROUND)
		return 0;

	OnCaught(pVictim->GetPlayer(), pKiller);

	char aBuf[512];
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;
		if(pPlayer->m_KillerId == -1)
			continue;
		if(pPlayer->GetCid() == pVictim->GetPlayer()->GetCid())
			continue;
		if(pPlayer->m_KillerId != pVictim->GetPlayer()->GetCid())
			continue;

		// victim's victims
		str_format(aBuf, sizeof(aBuf), "You respawned because '%s' died", Server()->ClientName(pVictim->GetPlayer()->GetCid()));
		ReleasePlayer(pPlayer, aBuf);
	}

	pVictim->GetPlayer()->m_vVictimIds.clear();
	return 0;
}

// called before spam protection on client team join request
bool CGameControllerZcatch::OnSetTeamNetMessage(const CNetMsg_Cl_SetTeam *pMsg, int ClientId)
{
	if(GameServer()->m_World.m_Paused)
		return false;
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	int Team = pMsg->m_Team;
	if(Server()->IsSixup(ClientId) && g_Config.m_SvSpectatorVotes && g_Config.m_SvSpectatorVotesSixup && pPlayer->m_IsFakeDeadSpec)
	{
		if(Team == TEAM_SPECTATORS)
		{
			// when a sixup fake spec tries to join spectators
			// he actually tries to join team red
			Team = TEAM_RED;
		}
	}

	if(
		(Server()->IsSixup(ClientId) && pPlayer->m_IsDead && Team == TEAM_SPECTATORS) ||
		(!Server()->IsSixup(ClientId) && pPlayer->m_IsDead && Team == TEAM_RED))
	{
		pPlayer->m_WantsToJoinSpectators = !pPlayer->m_WantsToJoinSpectators;
		char aBuf[512];
		if(pPlayer->m_WantsToJoinSpectators)
			str_format(aBuf, sizeof(aBuf), "You will join the spectators once '%s' dies", Server()->ClientName(pPlayer->m_KillerId));
		else
			str_format(aBuf, sizeof(aBuf), "You will join the game once '%s' dies", Server()->ClientName(pPlayer->m_KillerId));

		GameServer()->SendBroadcast(aBuf, ClientId);
		return true;
	}

	return CGameControllerPvp::OnSetTeamNetMessage(pMsg, ClientId);
}

// called after spam protection on client team join request
bool CGameControllerZcatch::CanJoinTeam(int Team, int NotThisId, char *pErrorReason, int ErrorReasonSize)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[NotThisId];
	if(!pPlayer)
		return false;

	if(pPlayer->m_IsDead && Team != TEAM_SPECTATORS)
	{
		str_format(pErrorReason, ErrorReasonSize, "Wait until '%s' dies", Server()->ClientName(pPlayer->m_KillerId));
		return false;
	}
	return true;
}

void CGameControllerZcatch::UpdateCatchTicks(class CPlayer *pPlayer)
{
	char aBuf[512];
	int Ticks = Server()->Tick() - pPlayer->m_LastSetTeam;
	if(pPlayer->m_IsDead)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' was caught for %d ticks", Server()->ClientName(pPlayer->GetCid()), Ticks);
		pPlayer->m_Stats.m_TicksCaught += Ticks;
	}
	else if(!pPlayer->m_IsDead && pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' was in game for %d ticks", Server()->ClientName(pPlayer->GetCid()), Ticks);
		pPlayer->m_Stats.m_TicksInGame += Ticks;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "spectating player '%s' left or joined (no ticks tracked)", Server()->ClientName(pPlayer->GetCid()));
	}

	if(g_Config.m_SvDebugStats > 1)
		SendChat(-1, TEAM_ALL, aBuf);
}

void CGameControllerZcatch::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	UpdateCatchTicks(pPlayer);

	CGameControllerInstagib::DoTeamChange(pPlayer, Team, DoChatMsg);

	CheckGameState();
}

void CGameControllerZcatch::CheckGameState()
{
	int ActivePlayers = NumActivePlayers();

	const int CompetitiveMin = 10;

	if(ActivePlayers >= CompetitiveMin && CatchGameState() != ECatchGameState::RUNNING_COMPETITIVE)
	{
		SendChatTarget(-1, "Enough players connected. Starting competitive game!");
		SetCatchGameState(ECatchGameState::RUNNING_COMPETITIVE);
	}
	else if(ActivePlayers >= g_Config.m_SvZcatchMinPlayers && CatchGameState() == ECatchGameState::WAITING_FOR_PLAYERS)
	{
		SendChatTarget(-1, "Enough players connected. Starting game!");
		SetCatchGameState(ECatchGameState::RUNNING);
	}
	else if(ActivePlayers < CompetitiveMin && CatchGameState() == ECatchGameState::RUNNING_COMPETITIVE)
	{
		SendChatTarget(-1, "Not enough players connected anymore. Starting casual game!");
		SetCatchGameState(ECatchGameState::RUNNING);
	}
}

int CGameControllerZcatch::GetAutoTeam(int NotThisId)
{
	if(IsCatchGameRunning() && GetHighestSpreeClientId() != -1)
	{
		return TEAM_SPECTATORS;
	}

	return CGameControllerInstagib::GetAutoTeam(NotThisId);
}

void CGameControllerZcatch::OnPlayerConnect(CPlayer *pPlayer)
{
	// SetTeam is not called on join and m_LastSetTeam is initialized to zero
	// m_LastSetTeam is used to track the caught and in game ticks
	// to avoid getting a wrong (too high tick count on join)
	// we set it manually here
	//
	// this also means that zCatch now has a team change cool down on first join
	if(!pPlayer->m_LastSetTeam)
	{
		pPlayer->m_LastSetTeam = Server()->Tick();
	}

	CGameControllerInstagib::OnPlayerConnect(pPlayer);
	CheckGameState();

	int KillerId = GetHighestSpreeClientId();
	if(KillerId != -1)
	{
		// avoid team change message by pre setting it
		pPlayer->SetTeamRaw(TEAM_SPECTATORS);
		KillPlayer(pPlayer, GameServer()->m_apPlayers[KillerId]);

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' is now spectating you (selfkill to release them)", Server()->ClientName(pPlayer->GetCid()));
		SendChatTarget(KillerId, aBuf);
	}
	// complicated way of saying not tournament mode
	else if(CGameControllerInstagib::GetAutoTeam(pPlayer->GetCid()) != TEAM_SPECTATORS && pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		// auto join running games if nobody made a kill yet
		// SetTeam will kill us and delay the spawning
		// so you are stuck in the scoreboard for a second when joining a active round
		// but lets call that a feature for now so you have to to get ready
		pPlayer->SetTeam(TEAM_RED);
	}

	m_aBodyColors[pPlayer->GetCid()] = GetBodyColor(0);
	SetCatchColors(pPlayer);
	SendSkinBodyColor7(pPlayer->GetCid(), pPlayer->m_TeeInfos.m_ColorBody);

	if(CatchGameState() == ECatchGameState::WAITING_FOR_PLAYERS)
		SendChatTarget(pPlayer->GetCid(), "Waiting for more players to start the round.");
	else if(CatchGameState() == ECatchGameState::RELEASE_GAME)
		SendChatTarget(pPlayer->GetCid(), "This is a release game.");
}

void CGameControllerZcatch::OnPlayerDisconnect(class CPlayer *pDisconnectingPlayer, const char *pReason)
{
	UpdateCatchTicks(pDisconnectingPlayer);

	CGameControllerInstagib::OnPlayerDisconnect(pDisconnectingPlayer, pReason);

	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		pPlayer->m_vVictimIds.erase(std::remove(pPlayer->m_vVictimIds.begin(), pPlayer->m_vVictimIds.end(), pDisconnectingPlayer->GetCid()), pPlayer->m_vVictimIds.end());
	}
}

bool CGameControllerZcatch::OnEntity(int Index, int x, int y, int Layer, int Flags, bool Initial, int Number)
{
	CGameControllerInstagib::OnEntity(Index, x, y, Layer, Flags, Initial, Number);
	return false;
}

bool CGameControllerZcatch::DoWincheckRound()
{
	if(IsCatchGameRunning() && NumNonDeadActivePlayers() <= 1)
	{
		for(CPlayer *pPlayer : GameServer()->m_apPlayers)
		{
			if(!pPlayer)
				continue;

			// this player ended the round
			if(!pPlayer->m_IsDead && IsCatchGameRunning() && pPlayer->GetTeam() != TEAM_SPECTATORS)
			{
				char aBuf[512];
				str_format(aBuf, sizeof(aBuf), "'%s' won the round.", Server()->ClientName(pPlayer->GetCid()));
				if(!IsWinner(pPlayer, 0, 0))
				{
					// if the win did not count because there were less than 10
					// players we still give the winner points
					int Points = PointsForWin(pPlayer);
					pPlayer->m_Stats.m_Points += Points;
					str_format(aBuf, sizeof(aBuf), "'%s' won the round and gained %d points.", Server()->ClientName(pPlayer->GetCid()), Points);
				}
				SendChat(-1, TEAM_ALL, aBuf);
			}
		}

		EndRound();

		for(CPlayer *pPlayer : GameServer()->m_apPlayers)
		{
			if(!pPlayer)
				continue;

			// only release players that actually died
			// not all spectators
			if(pPlayer->m_IsDead)
			{
				pPlayer->m_IsDead = false;
				pPlayer->SetTeamNoKill(TEAM_RED);
			}
		}

		return true;
	}
	return false;
}

void CGameControllerZcatch::Snap(int SnappingClient)
{
	CGameControllerInstagib::Snap(SnappingClient);
}
