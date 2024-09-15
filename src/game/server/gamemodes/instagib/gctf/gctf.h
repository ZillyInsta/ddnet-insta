#ifndef GAME_SERVER_GAMEMODES_INSTAGIB_GCTF_GCTF_H
#define GAME_SERVER_GAMEMODES_INSTAGIB_GCTF_GCTF_H

#include <game/server/instagib/extra_columns.h>
#include <game/server/instagib/sql_stats_player.h>

#include <game/server/gamemodes/instagib/ctf.h>
#include <optional>

class CGCTFColumns : public CExtraColumns
{
public:
	const char *CreateTable() override
	{
		return
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) sql_name "  " sql_type "  DEFAULT " default ","
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
			;
	}

	const char *SelectColumns() override
	{
		return
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) ", " sql_name
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
			;
	}

	const char *InsertColumns() override { return SelectColumns(); }

	const char *UpdateColumns() override
	{
		return
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) ", " sql_name " = ? "
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
			;
	}

	const char *InsertValues() override
	{
		return
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) ", ?"
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
			;
	}

	void InsertBindings(int *pOffset, IDbConnection *pSqlServer, const CSqlStatsPlayer *pStats) override
	{
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) \
		if(HasValue(pStats->m_##name)) \
			pSqlServer->Bind##bind_type((*pOffset)++, GetValue(pStats->m_##name)); \
		else \
			pSqlServer->BindNull((*pOffset)++);
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
	}

	void UpdateBindings(int *pOffset, IDbConnection *pSqlServer, const CSqlStatsPlayer *pStats) override
	{
		InsertBindings(pOffset, pSqlServer, pStats);
	}

	void ReadAndMergeStats(int *pOffset, IDbConnection *pSqlServer, CSqlStatsPlayer *pOutputStats, const CSqlStatsPlayer *pNewStats) override
	{
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) \
	if(pSqlServer->IsNull(*pOffset)) \
		pOutputStats->m_##name = pNewStats->m_##name; \
	else \
		pOutputStats->m_##name = pNewStats->Merge##bind_type##merge_method(pSqlServer->Get##bind_type((*pOffset)++), pNewStats->m_##name); \
	dbg_msg("gctf", "db[%d]=%d round=%d => merge=%d", \
	 (*pOffset) - 1, pSqlServer->Get##bind_type((*pOffset) - 1), pNewStats->m_##name, HasValue(pOutputStats->m_##name) ? GetValue(pOutputStats->m_##name) : 0);
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
	}
};

class CGameControllerGCTF : public CGameControllerInstaBaseCTF
{
public:
	CGameControllerGCTF(class CGameContext *pGameServer);
	~CGameControllerGCTF() override;

	void OnCharacterSpawn(class CCharacter *pChr) override;
	void Tick() override;
};
#endif
