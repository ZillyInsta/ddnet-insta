#ifndef GAME_SERVER_GAMEMODES_VANILLA_DM_DM_H
#define GAME_SERVER_GAMEMODES_VANILLA_DM_DM_H

#include <game/server/instagib/extra_columns.h>

#include "../base_vanilla.h"

class CDmColumns : public CExtraColumns
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
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) pSqlServer->Bind##bind_type((*pOffset)++, pStats->m_##name);
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
	}

	void UpdateBindings(int *pOffset, IDbConnection *pSqlServer, const CSqlStatsPlayer *pStats) override
	{
		InsertBindings(pOffset, pSqlServer, pStats);
	}

	void Dump(const CSqlStatsPlayer *pStats, const char *pSystem = "stats") const override
	{
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) \
	dbg_msg(pSystem, "  %s: %d", sql_name, pStats->m_##name);
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
	}

	void MergeStats(CSqlStatsPlayer *pOutputStats, const CSqlStatsPlayer *pNewStats) override
	{
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) \
	pOutputStats->m_##name = Merge##bind_type##merge_method(pOutputStats->m_##name, pNewStats->m_##name);
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
	}

	void ReadAndMergeStats(int *pOffset, IDbConnection *pSqlServer, CSqlStatsPlayer *pOutputStats, const CSqlStatsPlayer *pNewStats) override
	{
#define MACRO_ADD_COLUMN(name, sql_name, sql_type, bind_type, default, merge_method) \
	pOutputStats->m_##name = Merge##bind_type##merge_method(pSqlServer->Get##bind_type((*pOffset)++), pNewStats->m_##name);
#include "sql_columns.h"
#undef MACRO_ADD_COLUMN
	}
};

class CGameControllerDM : public CGameControllerVanilla
{
public:
	CGameControllerDM(class CGameContext *pGameServer);
	~CGameControllerDM() override;

	void OnCharacterSpawn(class CCharacter *pChr) override;
	void Tick() override;
};
#endif
