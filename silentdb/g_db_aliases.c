/*
	Module implements all of the optional aliases database.

	File structure:

	File Header
	n*
	Player Header
	m*
	Player Aliases

	Every player has a db_alias_t structure. If the player renames or disconnects, this data is updated to the alias db.

	Module tries to avoid fragmenting the server memory badly by attempting to always allocate bigger chunks. However, it does not do
	real memory pooling and there will likely be fragmentation eventually.

	Operation:

	Players that are handled by updates are always buffered. Buffering happens only with updates. Aliases of the buffered players are
	always kept in the last used first in the list order. They are also written to the file in this order.

	Only buffered players are written to the file unless the file is truncated. Before closing the aliases database, the game should
	ensure update of all the player aliases it wants to be stored.
*/

#include "g_local.h"
#include "g_db_filehandling.h"
#include "g_db_aliases.h"

#define DB_ALIASES_VERSION "SLEnT UADB v0.4\0"
#define DB_ALIASES_VERSIONSIZE 16
#define DB_ALIASES_FILENAME "useradb.db"

//#define DEBUG_ALIASES 1			// debug aliases database

typedef enum {
	ALIASES_ACTION_NONE = 0,		//
	ALIASES_ACTION_REMOVE = 1,		// the player record/alias is to be removed
	ALIASES_ACTION_UPDATE = 2,		// the player record is to be updated to file
	ALIASES_ACTION_DIRTY = 4,		// the db file is to be truncated and rewritten
	ALIASES_ACTION_SKIP = 8			// the record is skipped when writing to file
} db_aliases_actions_t;

typedef struct db_aliases_fileheader_s {
	char	db_version[DB_ALIASES_VERSIONSIZE];
	uint32_t	players_count;
	uint32_t	records_count;
} db_aliases_fileheader_t;

// structure is used only in the file
typedef struct db_playeralias_header_s {
	uint8_t		guid[32];
	uint32_t	guidHash;
	uint32_t	numberOfRecords;
} db_playeralias_header_t;

typedef struct db_aliases_aliasesrecord_s {
	db_alias_t	alias;
	uint32_t	actions;
} db_aliases_aliasesrecord_t;

typedef struct db_aliases_insertrecord_s {
	db_alias_t	alias;
	struct db_aliases_insertrecord_s *next;
} db_aliases_insertrecord_t;

// structure is used only in the program, not in the file, the header is copied to the root,
// the alias records are copied into the allocated array of records
typedef struct db_playeraliases_s {
	uint8_t		guid[32];
	uint32_t	guidHash;
	uint32_t	numberOfRecords;
	db_aliases_aliasesrecord_t	*records;
	uint32_t	numberOfInsertRecords;
	db_aliases_insertrecord_t	*insertlist;
	uint32_t	newRecords;
	uint32_t	filePos;		// the player header position in the db file
	uint32_t	actions;
} db_playeraliases_t;

typedef struct db_buffered_player_s {
	db_playeraliases_t	player;
	struct db_buffered_player_s *next;
} db_buffered_player_t;

typedef struct db_aliases_info_s {
	db_aliases_fileheader_t file_header;	// read and written to file
	FILE*					aliases_file;
	db_playeraliases_t		*players;		// players read from the file
	uint32_t				player_count;
	// alias blocks that are read from the file are placed into the memory pool
	uint8_t					*memory_pool;	// records that are read from the file, accessed through players
	uint32_t				pool_size;
	uint32_t				pool_index;
	db_buffered_player_t	*buffer;
	//
	qboolean				aliases_inuse;	// if the database is usable or not
	uint32_t				actions;
} db_aliases_info_t;

static db_aliases_info_t aliases_info;
static db_playeraliases_t *searchedPlayer;
static db_aliases_insertrecord_t *searchedRecord;
static uint32_t positionIndex;

// static memory pools
// player buffer
#define BUFFER_POOL_SIZE (sizeof(db_buffered_player_t) * 96) // 3 times the normal high populated server
static uint8_t buffer_pool[BUFFER_POOL_SIZE];
static uint32_t buffer_pool_index;
// player aliases
#define ALIASES_POOL_SIZE (sizeof(db_aliases_insertrecord_t) * (2 * MAX_CLIENTS)) // 4 times the normal high populated server
static uint8_t aliases_pool[ALIASES_POOL_SIZE];
static uint32_t aliases_pool_index;

// alias searching
#define ALIASES_DB_MAXSEARCHCACHE 256

typedef struct db_aliases_searchedplayer_aliases_s {
	db_playeraliases_t	*player;
	db_alias_t			*aliases[ALIASES_DB_MAXALIASES_FORONERESULT];
} db_aliases_searchedplayer_aliases_t;

typedef struct db_aliases_searchcache_s {
	char		search_pattern[MAX_NAME_LENGTH];  // needed for fine graining the results
	uint32_t	used_cache;						// the amount of players in search cache
	uint32_t	iterator;						// the iterator used with the result fetching
	// pointers to the player data, or NULL
	const db_playeraliases_t *results[ALIASES_DB_MAXSEARCHCACHE];  // the last results as indexes
} db_aliases_searchcache_t;

static db_aliases_searchcache_t search_cache;

/*
	Semi memory pool handling
*/

static db_aliases_insertrecord_t* DB_AllocAliasInsert(void)
{
	uint8_t *record;

	// if memory insufficient, normal malloc one by one
	if( aliases_pool_index + sizeof(db_aliases_insertrecord_t) >= ALIASES_POOL_SIZE ) {
		record = malloc(sizeof(db_aliases_insertrecord_t));
	} else {
		record = &aliases_pool[aliases_pool_index];
		aliases_pool_index += sizeof(db_aliases_insertrecord_t);
	}

	return (db_aliases_insertrecord_t*)record;
}

static void DB_FreeAliasInsert(void *address)
{
	if( address == NULL ) {
		return;
	}

	if( (uint8_t*)address >= &aliases_pool[0] && (uint8_t*)address < &aliases_pool[ALIASES_POOL_SIZE] ) {
		return;
	}

	free(address);
}

static db_buffered_player_t* DB_AllocPlayerAlias(void)
{
	uint8_t *alias;

	// if memory insufficient, normal malloc one by one
	if( buffer_pool_index + sizeof(db_buffered_player_t) >= BUFFER_POOL_SIZE ) {
		alias = malloc(sizeof(db_buffered_player_t));
	} else {
		alias = &buffer_pool[buffer_pool_index];
		buffer_pool_index += sizeof(db_buffered_player_t);
	}

	return (db_buffered_player_t*)alias;
}

static void DB_FreePlayerAlias(void *address)
{
	if( address == NULL ) {
		return;
	}

	if( (uint8_t*)address >= &buffer_pool[0] && (uint8_t*)address < &buffer_pool[BUFFER_POOL_SIZE] ) {
		return;
	}

	free(address);
}

static db_aliases_insertrecord_t* DB_GetAliasInsertRecord(const db_playeraliases_t *player, const char *cleanName)
{
	db_aliases_insertrecord_t *list = player->insertlist;

	while( list ) {
		if( !Q_stricmp( list->alias.clean_name, cleanName ) ) {
			return list;
		}
		list = list->next;
	}

	return NULL;
}

static db_playeraliases_t* DB_GetPlayer(const uint32_t guidHash, const char *guid)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *buffered = info->buffer;
	db_playeraliases_t *cachedPlayer = NULL;
	uint32_t lastIndex = 0;
	uint32_t i;

	while( buffered ) {
		if( buffered->player.guidHash == guidHash && !memcmp(buffered->player.guid, guid, sizeof(buffered->player.guid)) ) {
			// found the player from player buffer, returning that
			return &buffered->player;
		}
		buffered = buffered->next;
	}
	// search the player from the loaded data
	lastIndex = info->player_count;
	for( i = 0; i < lastIndex ; i++ ) {
		cachedPlayer = &info->players[i];
		if( cachedPlayer->guidHash == guidHash && !memcmp(cachedPlayer->guid, guid, sizeof(cachedPlayer->guid)) ) {
			return cachedPlayer;
		}
	}

	return NULL;
}

static db_playeraliases_t* DB_GetPlayerOrCreate(const uint32_t guidHash, const char *guid)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *bufferedPlayer = info->buffer;
	db_buffered_player_t *player = NULL;
	db_playeraliases_t *cachedPlayer = NULL;
	uint32_t lastIndex = 0;
	uint32_t i;
	qboolean found;

	while( bufferedPlayer ) {
		player = bufferedPlayer;
		if( player->player.guidHash == guidHash && !memcmp(player->player.guid, guid, sizeof(player->player.guid)) ) {
			// found the player from player buffer, returning that
			return &player->player;
		}
		bufferedPlayer = bufferedPlayer->next;
	}
	// store back the last non NULL player, for the pointer work
	if( player ) {
		bufferedPlayer = player;
	}
	// search the player from the loaded data
	found = qfalse;
	lastIndex = info->player_count;
	for( i = 0; i < lastIndex ; i++ ) {
		cachedPlayer = &info->players[i];
		if( cachedPlayer->guidHash == guidHash && !memcmp(cachedPlayer->guid, guid, sizeof(cachedPlayer->guid)) ) {
			found = qtrue;
			break;
		}
	}

	// player is not in the buffer, it is created and added to it now
	player = DB_AllocPlayerAlias();

	if( found ) {
		// copy data if the player was found from old
		memcpy(&player->player, cachedPlayer, sizeof(player->player));
		cachedPlayer->actions |= ALIASES_ACTION_SKIP;
	} else {
		player->player.guidHash = guidHash;
		memcpy(player->player.guid, guid, sizeof(player->player.guid));
		player->player.records = NULL;
		player->player.numberOfRecords = 0;
		// mark the file for rewrite
		aliases_info.actions = ALIASES_ACTION_DIRTY;
	}
	// insert list is always empty first
	player->player.insertlist = NULL;
	player->player.numberOfInsertRecords = 0;
	// no new records yet
	player->player.newRecords = 0;

	// set up pointers
	player->next = NULL;
	if( bufferedPlayer ) {
		bufferedPlayer->next = player;
	} else {
		info->buffer = player;
	}

	return &player->player;
}

/*
	Aliases file handling
*/
static int DB_Write_AliasesDBheader(FILE *handle)
{
	if( handle == NULL ) {
		return -1;
	}

	memcpy(aliases_info.file_header.db_version, DB_ALIASES_VERSION, DB_ALIASES_VERSIONSIZE);

	return G_DB_WriteBlockToFile(handle, &aliases_info.file_header, sizeof(aliases_info.file_header), 0);
}

static int DB_CreateAliasesFile(void)
{
	// some information that is needed
	G_DB_File_Open(&aliases_info.aliases_file, DB_ALIASES_FILENAME, DB_FILEMODE_TRUNCATE);

	if( !aliases_info.aliases_file ) {
		return -1;
	}

	aliases_info.file_header.players_count = 0;
	aliases_info.file_header.records_count = 0;

	if( !DB_Write_AliasesDBheader(aliases_info.aliases_file) ) {
		aliases_info.aliases_inuse = qtrue;
	}

	return 0;
}

#ifdef DEBUG_ALIASES
static int DB_DebugReadAliasesFile(void)
{
	db_playeralias_header_t playerHeader;
	db_aliases_info_t		*info = &aliases_info;
	uint32_t				j, total, totalCount;
	int						filePos;
	uint32_t				playerLimit, recordLimit;
	char					guidString[33];
	//int numberOfPlayers = 0;
	int i, users;

	// init file header
	G_DB_ReadBlockFromDBFile(aliases_info.aliases_file, &aliases_info.file_header, sizeof(aliases_info.file_header), 0);

	if( memcmp(aliases_info.file_header.db_version, DB_ALIASES_VERSION, DB_ALIASES_VERSIONSIZE) ) {
		return -2;
	}

	if( !(info->file_header.players_count > 0) ) {
		// empty file
		return -1;
	}

	G_LogPrintf("Opening aliases file in verbose mode\n");

	// allocate the memory for the player list
	info->players = malloc(info->file_header.players_count * sizeof(db_playeraliases_t));
	info->player_count = 0;
	playerLimit = info->file_header.players_count;
	// allocate the memory pool in one block, enough to hold all aliases
	aliases_info.pool_size += info->file_header.records_count * sizeof(db_aliases_aliasesrecord_t);
	info->pool_index = 0;
	recordLimit = info->file_header.records_count;

	if( !(aliases_info.pool_size > 0) ) {
		// empty file
		free(info->players);
		info->players = NULL;
		G_LogPrintf("Aliases file stores players but holds no aliases.\n");
		return -1;
	}

	aliases_info.memory_pool = malloc( aliases_info.pool_size );
	// two part read, first read header then read the aliase one by one
	users = aliases_info.file_header.players_count;
	G_LogPrintf("Trying to read %d players and total of %d aliases\n", users, recordLimit);
	totalCount = recordLimit;
	total = 0;
	for( i = 0 ; i < users ; i++ ) {
		if( !playerLimit ) {
			G_LogPrintf("Extra player encountered.\n");
			return -3;
		}
		filePos = G_DB_ReadBlockFromDBFile(aliases_info.aliases_file, &playerHeader, sizeof(playerHeader), -1);
		if( filePos == -1 ) {
			G_LogPrintf("Unexpected end of file. Missing player header. Still wanted to read %d players.\n", playerLimit);
			break;
		}
		memcpy(&info->players[i], &playerHeader, sizeof(playerHeader));
		info->players[i].filePos = filePos;
		info->players[i].newRecords = 0;

		Q_strncpyz(guidString, (const char*)&info->players[i].guid[0], sizeof(guidString));
		G_LogPrintf("%d: Reading aliases of a player with GUID (%s). Aliases for player: %d.\n", i, guidString, playerHeader.numberOfRecords);

		if( playerHeader.numberOfRecords ) {
			info->players[i].records = (db_aliases_aliasesrecord_t*)&info->memory_pool[info->pool_index];
			for( j = 0 ; j < playerHeader.numberOfRecords ; j++ ) {
				db_alias_t *alias;
				if( !recordLimit ) {
					G_LogPrintf("Unaccounted alias encountered.\n");
					return -3;
				}
				G_DB_ReadBlockFromDBFile(aliases_info.aliases_file, &info->memory_pool[info->pool_index], sizeof(db_alias_t), -1);
				info->pool_index += sizeof(db_aliases_aliasesrecord_t);
				info->players[i].records[j].actions = ALIASES_ACTION_NONE;
				recordLimit--;
				alias = &info->players[i].records[j].alias;
				total++;
				G_LogPrintf("  (%d/%d) %d: Alias: '%s', time played %d\n", total,  totalCount, (j+1), alias->name, alias->time_played);
			}
			//info->players[i].numberOfRecords == playerHeader.numberOfRecords;
			info->players[i].actions = ALIASES_ACTION_NONE;
			info->players[i].insertlist = NULL;
			info->players[i].numberOfInsertRecords = 0;
		}
		info->player_count++;
		playerLimit--;
	}

	return 0;
}
#endif

//	Return -3, bad file corrupt
//         -2, version mismatch
//         -1, empty file
//         0, all success
static int DB_ReadAliasesFile(void)
{
	db_playeralias_header_t playerHeader;
	db_aliases_info_t		*info = &aliases_info;
	uint32_t				j;
	int						filePos;
	uint32_t				playerLimit, recordLimit;
	//int numberOfPlayers = 0;
	int i, users;

	// init file header
	G_DB_ReadBlockFromDBFile(aliases_info.aliases_file, &aliases_info.file_header, sizeof(aliases_info.file_header), 0);

	if( memcmp(aliases_info.file_header.db_version, DB_ALIASES_VERSION, DB_ALIASES_VERSIONSIZE) ) {
		return -2;
	}

	if( !(info->file_header.players_count > 0) ) {
		// empty file
		return -1;
	}

	// allocate the memory for the player list
	info->players = malloc(info->file_header.players_count * sizeof(db_playeraliases_t));
	info->player_count = 0;
	playerLimit = info->file_header.players_count;
	// allocate the memory pool in one block, enough to hold all aliases
	aliases_info.pool_size += info->file_header.records_count * sizeof(db_aliases_aliasesrecord_t);
	info->pool_index = 0;
	recordLimit = info->file_header.records_count;

	if( !(aliases_info.pool_size > 0) ) {
		// empty file
		free(info->players);
		info->players = NULL;
		return -1;
	}

	aliases_info.memory_pool = malloc( aliases_info.pool_size );
	// two part read, first read header then read the aliase one by one
	users = aliases_info.file_header.players_count;
	for( i = 0 ; i < users ; i++ ) {
		if( !playerLimit ) {
			return -3;
		}
		filePos = G_DB_ReadBlockFromDBFile(aliases_info.aliases_file, &playerHeader, sizeof(playerHeader), -1);
		if( filePos == -1 ) {
			G_LogPrintf("  Unexpected end of file. Missing player header. Still wanted to read %d players.\n", playerLimit);
			break;
		}
		memcpy(&info->players[i], &playerHeader, sizeof(playerHeader));
		info->players[i].filePos = filePos;
		info->players[i].newRecords = 0;
		if( playerHeader.numberOfRecords ) {
			info->players[i].records = (db_aliases_aliasesrecord_t*)&info->memory_pool[info->pool_index];
			for( j = 0 ; j < playerHeader.numberOfRecords ; j++ ) {
				if( !recordLimit ) {
					return -3;
				}
				G_DB_ReadBlockFromDBFile(aliases_info.aliases_file, &info->memory_pool[info->pool_index], sizeof(db_alias_t), -1);
				info->pool_index += sizeof(db_aliases_aliasesrecord_t);
				info->players[i].records[j].actions = ALIASES_ACTION_NONE;
				recordLimit--;
			}
			//info->players[i].numberOfRecords == playerHeader.numberOfRecords;
			info->players[i].actions = ALIASES_ACTION_NONE;
			info->players[i].insertlist = NULL;
			info->players[i].numberOfInsertRecords = 0;
		}
		info->player_count++;
		playerLimit--;
	}

	return 0;
}

static int DB_WritePlayerToFile(db_playeraliases_t *player)
{
	db_aliases_insertrecord_t *insert;
	db_aliases_insertrecord_t *tmp;
	int numberOfRecords = 0;
	uint32_t i, oldRecords;
	int32_t limit = g_dbMaxAliases.integer;

	if( player->actions & (ALIASES_ACTION_SKIP | ALIASES_ACTION_REMOVE) ) {
		return 0;
	}

	if( player->filePos > 0 ) {
		G_DB_SetFilePosition(aliases_info.aliases_file, player->filePos);
	}

	// write the player header to the position, the beginning part of the db_playeraliases_t is the exact match to db_playeralias_header_t
	oldRecords = player->numberOfRecords;
	player->numberOfRecords += player->newRecords;
	if( (int32_t)player->numberOfRecords > limit ) {
		player->numberOfRecords = limit;
	}
	G_DB_WriteBlockToFile(aliases_info.aliases_file, player, sizeof(db_playeralias_header_t), -1);

	// write insert buffer, this is preoreder to the last used first order
	insert = player->insertlist;
	for(i = 0 ; i < player->numberOfInsertRecords && numberOfRecords < limit; i++ ) {
		G_DB_WriteBlockToFile(aliases_info.aliases_file, &insert->alias, sizeof(insert->alias), -1);
		tmp = insert;
		insert = insert->next;
		DB_FreeAliasInsert(tmp);
		numberOfRecords++;
	}
	// then write the remaining from the old records
	for(i = 0; i < oldRecords && numberOfRecords < limit ; i++ ) {
		if( player->records[i].actions & ALIASES_ACTION_SKIP ) {
			// this one was already written as part of the insert buffer
			continue;
		}
		G_DB_WriteBlockToFile(aliases_info.aliases_file, &player->records[i].alias, sizeof(player->records[0].alias), -1);
		numberOfRecords++;
	}

	player->actions = ALIASES_ACTION_SKIP;

	return numberOfRecords;
}

static void DB_WriteAliasesToFile( void )
{
	// this function only does in place writes, if new records are added, truncate write is needed
	db_buffered_player_t *buffered = aliases_info.buffer;
	db_buffered_player_t *tmp;

	if( !aliases_info.aliases_inuse ) {
		return;
	}

	G_DB_File_Open(&aliases_info.aliases_file, DB_ALIASES_FILENAME, DB_FILEMODE_UPDATE);

	// loop through the insert buffer and update all players there
	while( buffered ) {
		DB_WritePlayerToFile(&buffered->player);
		tmp = buffered;
		buffered = buffered->next;
		DB_FreePlayerAlias(tmp);
	}

	G_DB_File_Close(&aliases_info.aliases_file);
}

static void DB_WriteAliasesToFileTruncate(void)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *buffered = info->buffer;
	db_buffered_player_t *temp;
	uint32_t lastIndex = 0;
	uint32_t i, numberOfPlayers, numberOfRecords, tmp;

	if( !aliases_info.aliases_inuse ) {
		return;
	}

	G_DB_File_Open(&aliases_info.aliases_file, DB_ALIASES_FILENAME, DB_FILEMODE_TRUNCATE);

	// write empty header
	info->file_header.players_count = 0;
	info->file_header.records_count = 0;
	DB_Write_AliasesDBheader(info->aliases_file);

	// first write everyone in the insert buffer
	numberOfPlayers = 0;
	numberOfRecords = 0;
	//for( i = 0 ; i < lastIndex ; i++ ) {
	while( buffered ) {
		// manually clear the file position for sequentially writing everyone
		buffered->player.filePos = 0;
		tmp = DB_WritePlayerToFile(&buffered->player);
		if( tmp ) {
			numberOfRecords += tmp;
			numberOfPlayers++;
		}
		temp = buffered;
		buffered = buffered->next;
		DB_FreePlayerAlias(temp);
	}
	// now writing all the rest
	lastIndex = aliases_info.player_count;
	for( i = 0 ; i < lastIndex ; i++ ) {
		info->players[i].filePos = 0;
		tmp = DB_WritePlayerToFile(&info->players[i]);
		if( tmp ) {
			numberOfRecords += tmp;
			numberOfPlayers++;
		}
	}

	// filling the header data with correct values and writing it
	info->file_header.players_count = numberOfPlayers;
	info->file_header.records_count = numberOfRecords;
	DB_Write_AliasesDBheader(info->aliases_file);

	G_DB_File_Close(&aliases_info.aliases_file);
}

static db_aliases_aliasesrecord_t* GetAliasFromRecords(db_playeraliases_t *player, const char *cleanName)
{
	db_aliases_aliasesrecord_t *records = player->records;
	uint32_t i;

	for( i = 0 ; i < player->numberOfRecords ; i++ ) {
		if( records[i].actions & ALIASES_ACTION_SKIP ) {
			continue;
		}
		if( !Q_stricmpn(records[i].alias.clean_name, cleanName, sizeof(records[i].alias.clean_name)) ) {
			return &records[i];
		}
	}

	return NULL;
}

static void DB_CombineAliasDataForWrite(db_playeraliases_t *player)
{
	// if the block size is different from the actually used, then the file must be truncated
	// this is needed when the g_dbMaxAliases changes to smaller then it was
	// all the new aliases result truncates automatically
	if( player->numberOfRecords > (uint32_t)g_dbMaxAliases.integer ) {
		aliases_info.actions = ALIASES_ACTION_DIRTY;
		return;
	}
}

static void G_DB_AliasesUpdate(void)
{
	db_buffered_player_t *buffered = aliases_info.buffer;

	// loop through the insert buffer and do necessary
	while( buffered ) {
		DB_CombineAliasDataForWrite(&buffered->player);
		buffered = buffered->next;
	}
}

int G_DB_InitAliases(void)
{
	int retVal;

	aliases_info.aliases_inuse = qfalse;

	if( g_dbMaxAliases.integer <= 0 ) {
		return -3;
	}

	G_LogPrintf("  * Reading aliases database.\n");

	// if cvar enabled, check that directory exists
	G_DB_File_Open(&aliases_info.aliases_file, DB_ALIASES_FILENAME, DB_FILEMODE_READ);

	if( !aliases_info.aliases_file ) {
		G_LogPrintf("  Aliases database file does not exist.\n");
		if( DB_CreateAliasesFile() == -1 ) {
			G_LogPrintf("  Failed creating user database file %s.\n", DB_ALIASES_FILENAME);
			return -1;
		}
		G_LogPrintf("  New user database file %s created.\n", DB_ALIASES_FILENAME);
		return 1;
	} else {
#ifdef DEBUG_ALIASES
		retVal = DB_DebugReadAliasesFile();
#else
		retVal = DB_ReadAliasesFile();
#endif
		if( retVal == -3 ) {
			G_LogPrintf("  Database file is corrupted and can not be used.\n");
			return -2;
		} else if( retVal == -2 ) {
			G_LogPrintf("  Existing database file is for wrong server version or corrupted.\n");
			return -2;
		} else if ( retVal == -1 ) {
			G_LogPrintf("  Aliases database is empty.\n");
		} else {
			G_LogPrintf("  Read total of %d aliases for %d players.\n", aliases_info.file_header.records_count, aliases_info.file_header.players_count);
		}
	}

	G_DB_File_Close(&aliases_info.aliases_file);

	// pool indexes
	buffer_pool_index = 0;
	aliases_pool_index = 0;

	// buffer
	aliases_info.buffer = NULL;

	aliases_info.aliases_inuse = qtrue;

	return 0;
}

void G_DB_CloseAliases(void)
{
	if( !aliases_info.aliases_inuse ) {
		return;
	}

	// protect the database from corruption by admins setting the value to -1 during a map. (though should not be possible with unmodifed server engine)
	if( g_dbMaxAliases.integer > 0 ) {

		// this will also determine if truncate is needed even if no new data would be added
		G_DB_AliasesUpdate();

		if( aliases_info.actions & ALIASES_ACTION_DIRTY ) {
			DB_WriteAliasesToFileTruncate();
		} else {
			DB_WriteAliasesToFile();
		}
	}
	// free all dynamic memory
	free(aliases_info.memory_pool);
	free(aliases_info.players);
}

void G_DB_CleanUpAliases(void)
{
	db_aliases_info_t *info = &aliases_info;
	db_playeraliases_t *cachedPlayer = NULL;
	int32_t unlinkableAliases = 0;
	uint32_t i, lastIndex;

	if( !aliases_info.aliases_inuse ) {
		return;
	}

	G_LogPrintf("  * Aliases database:\n");

	lastIndex = info->player_count;
	for( i = 0; i < lastIndex ; i++ ) {
		cachedPlayer = &info->players[i];
		if( G_DB_GetUserHandleWithoutBuffering( (const char*)cachedPlayer->guid ) == NULL ) {
			unlinkableAliases++;
			cachedPlayer->actions |= ALIASES_ACTION_REMOVE;
		}
	}

	if( unlinkableAliases ) {
		G_LogPrintf("  Issued delete to aliases from %d players without associated records in the main database.\n", unlinkableAliases);
		// mark the file into truncate
		info->actions = ALIASES_ACTION_DIRTY;
	} else {
		G_LogPrintf("  All records in the aliases database have associated player records in the main database.\n");
	}
}

void G_DB_RemoveAliases(const char *guid, uint32_t guidHash)
{
	db_playeraliases_t	*player;
	char guid_l[32];
	int i;

	if( !aliases_info.aliases_inuse ) {
		return;
	}

	if( !guidHash ) {
		// set up the data, ensure uppercase letters and create hash value
		for(i=0; i < 32 && guid[i] ;i++) {
			guid_l[i]=toupper(guid[i]);
		}
		if(i!=32) { return; }

		guidHash = BG_hashword((const uint32_t*)guid_l, 8, 0);

		player = DB_GetPlayer(guidHash, guid_l);
	} else {
		player = DB_GetPlayer(guidHash, guid);
	}

	if( !player ) {
		return;
	}

	player->actions |= ALIASES_ACTION_REMOVE;
	// truncate the file, no matter if that player removed wasn't even written in the old file yet
	aliases_info.actions = ALIASES_ACTION_DIRTY;
}

qboolean G_DB_RemoveAliasesShortGUID(const char *shortGuid)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *buffered = info->buffer;
	db_playeraliases_t *cachedPlayer = NULL;
	db_playeraliases_t *player = NULL;
	uint32_t i, lastIndex;

	if( !aliases_info.aliases_inuse ) {
		return qfalse;
	}

	// first buffer
	searchedPlayer = NULL;
	while( buffered ) {
		player = &buffered->player;
		buffered = buffered->next;
		if( player->actions & ALIASES_ACTION_REMOVE ) {
			continue;
		}
		if( !memcmp(&player->guid[24], shortGuid, 8) ) {
			searchedPlayer = player;
			break;
		}
	}

	// now all the file records
	if( !searchedPlayer ) {
		lastIndex = info->player_count;
		for( i = 0; i < lastIndex ; i++ ) {
			cachedPlayer = &info->players[i];
			if( cachedPlayer->actions & (ALIASES_ACTION_REMOVE | ALIASES_ACTION_SKIP) ) {
				continue;
			}
			if( !memcmp(&cachedPlayer->guid[24], shortGuid, 8) ) {
				searchedPlayer = cachedPlayer;
				break;
			}
		}
	}

	if( !searchedPlayer ) {
		return qfalse;
	}

	searchedPlayer->actions |= ALIASES_ACTION_REMOVE;
	// truncate the file, no matter if that player removed wasn't even written in the old file yet
	aliases_info.actions = ALIASES_ACTION_DIRTY;

	return qtrue;
}

/**
 *	Function updates alias data or inserts new one if needed.
 */
void G_DB_UpdateAlias(const char *guid, const db_alias_t *alias, uint32_t guidHash)
{
	db_playeraliases_t	*player;
	db_aliases_insertrecord_t *oldAlias;
	db_aliases_aliasesrecord_t *oldCachedAlias;
	db_aliases_insertrecord_t *aliasInsert = NULL;
	uint32_t i;
	char guid_l[32];

	if( !aliases_info.aliases_inuse ) {
		return;
	}

	if( !guidHash ) {
		// set up the data, ensure uppercase letters and create hash value
		for(i=0; i < 32 && guid[i] ;i++) {
			guid_l[i]=toupper(guid[i]);
		}
		if(i!=32) { return; }

		guidHash = BG_hashword((const uint32_t*)guid_l, 8, 0);

		player = DB_GetPlayerOrCreate(guidHash, guid_l);
	} else {
		player = DB_GetPlayerOrCreate(guidHash, guid);
	}

	if( !player ) {
		return;
	}
	// append the new alias, at this time the aliases are searched to find if the alias already exists
	oldAlias = DB_GetAliasInsertRecord(player, alias->clean_name);

	if( oldAlias ) {
		oldAlias->alias.last_seen = alias->last_seen;
		oldAlias->alias.time_played += alias->time_played;
		Q_strncpyz(oldAlias->alias.name, alias->name, sizeof(oldAlias->alias.name));
		// needs to be shifted to the front, since it is the last used
		if( oldAlias != player->insertlist ) {
			db_aliases_insertrecord_t *tmp = player->insertlist;
			// find the one pointing to this
			while( tmp ) {
				if( tmp->next == oldAlias ) {
					break;
				}
				tmp = tmp->next;
			}
			if( tmp && tmp->next ) {
				tmp->next = oldAlias->next;
			}
			// update the front
			oldAlias->next = player->insertlist;
			player->insertlist = oldAlias;
		}
		return;
	}

	// alias was not found so creating new one into the insert list
	aliasInsert = DB_AllocAliasInsert();

	// take the old records in use if possible
	oldCachedAlias = GetAliasFromRecords(player, alias->clean_name);

	if( oldCachedAlias ) {
		// copy old data and mark the cached record to be skipped
		memcpy(&aliasInsert->alias, &oldCachedAlias->alias, sizeof(aliasInsert->alias));
		aliasInsert->alias.last_seen = alias->last_seen;
		aliasInsert->alias.time_played += alias->time_played;
		oldCachedAlias->actions = ALIASES_ACTION_SKIP;
	} else {
		// completely new, also mark the file for truncate
		aliasInsert->alias.first_seen = alias->first_seen;
		aliasInsert->alias.last_seen = alias->last_seen;
		aliasInsert->alias.time_played = alias->time_played;
		Q_strncpyz(aliasInsert->alias.name, alias->name, sizeof(aliasInsert->alias.name));
		Q_strncpyz(aliasInsert->alias.clean_name, alias->clean_name, sizeof(aliasInsert->alias.clean_name));

		player->newRecords++;
		aliases_info.actions = ALIASES_ACTION_DIRTY;
	}
	// adjust pointers with new insert record
	aliasInsert->next = player->insertlist;
	player->insertlist = aliasInsert;
	player->numberOfInsertRecords++;
	// done
}

/**
	Function returns the number of aliases and sets the internal iterator to the first alias if available.

	@param guid Is the 32 character silEnT GUID of the player.
	@param start The first record that is returned from G_DB_GetNextAlias
	@return the number of aliases to read with G_DB_GetNextAlias function calls or, -1 if aliases not in use, -2 if bad guid
*/
int G_DB_GetAliases(const char *guid, const int start)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *buffered = info->buffer;
	db_playeraliases_t *player = NULL;
	uint32_t guidHash;
	uint32_t i;
	char guid_l[32];
	qboolean found;

	if( !aliases_info.aliases_inuse ) {
		return -1;
	}

	// set up the data, ensure uppercase letters and create hash value
	for(i=0; i < 32 && guid[i] ;i++) {
		guid_l[i]=toupper(guid[i]);
	}
	if(i!=32) { return -2; }

	guidHash = BG_hashword((const uint32_t*)guid_l, 8, 0);

	// only from buffer
	found = qfalse;
	while( buffered ) {
		player = &buffered->player;
		buffered = buffered->next;
		if( player->actions & ALIASES_ACTION_REMOVE ) {
			continue;
		}
		if( player->guidHash == guidHash && !memcmp(player->guid, guid_l, sizeof(player->guid)) ) {
			searchedPlayer = player;
			found = qtrue;
			break;
		}
	}

	if( !found ) {
		return -3;
	}

	// moving the position to the correct place
	// start from insert buffer and then the old records
	if( start < 0 ) {
        positionIndex = 0;
	} else {
        positionIndex = start;
	}

	if( positionIndex > (int32_t)player->numberOfInsertRecords ) {
		searchedRecord = NULL;
		positionIndex -= player->numberOfInsertRecords;
	} else {
		searchedRecord = player->insertlist;
		// always skipping the first, it was just updated to the aliases
		for( i=0 ; i < positionIndex && searchedRecord ; i++) {
			searchedRecord = searchedRecord->next;
		}
		// once the insert list is done, this points to the old data
		positionIndex = 0;
	}

	return (player->newRecords + player->numberOfRecords);
}

const db_alias_t* G_DB_GetNextAlias(void)
{
	db_alias_t *alias;
	uint32_t highLimit;

	if( !searchedPlayer ) {
		return NULL;
	}

	if( searchedRecord ) {
		alias = &searchedRecord->alias;
		searchedRecord = searchedRecord->next;
		return alias;
	}

	highLimit = searchedPlayer->numberOfRecords;

	while( positionIndex < highLimit && (searchedPlayer->records[positionIndex].actions & ALIASES_ACTION_SKIP) ) {
		positionIndex++;
	}

	if( positionIndex >= highLimit ) {
		return NULL;
	}

	alias = &searchedPlayer->records[positionIndex].alias;
	positionIndex++;

	return alias;
}

int G_DB_SearchAliasesShortGUID(const char *guid, const int start)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *buffered = info->buffer;
	db_playeraliases_t *cachedPlayer = NULL;
	db_playeraliases_t *player = NULL;
	uint32_t i, lastIndex;

	if( !info->aliases_inuse ) {
		return -1;
	}

	// first buffer
	searchedPlayer = NULL;
	while( buffered ) {
		player = &buffered->player;
		buffered = buffered->next;
		if( player->actions & ALIASES_ACTION_REMOVE ) {
			continue;
		}
		if( !memcmp(&player->guid[24], guid, 8) ) {
			searchedPlayer = player;
			break;
		}
	}

	// now all the file records
	if( !searchedPlayer ) {
		lastIndex = info->player_count;
		for( i = 0; i < lastIndex ; i++ ) {
			cachedPlayer = &info->players[i];
			if( cachedPlayer->actions & (ALIASES_ACTION_REMOVE | ALIASES_ACTION_SKIP) ) {
				continue;
			}
			if( !memcmp(&cachedPlayer->guid[24], guid, 8) ) {
				searchedPlayer = cachedPlayer;
				break;
			}
		}
	}

	if( !searchedPlayer ) {
		return 0;
	}

	// moving the position to the correct place
	// start from insert buffer and then the old records
	if( start < 0 ) {
        positionIndex = 0;
	} else {
        positionIndex = start;
	}

	if( positionIndex > (int32_t)searchedPlayer->numberOfInsertRecords ) {
		searchedRecord = NULL;
		positionIndex -= searchedPlayer->numberOfInsertRecords;
	} else {
		searchedRecord = searchedPlayer->insertlist;
		// always skipping the first, it was just updated to the aliases
		for( i=0 ; i < positionIndex && searchedRecord ; i++) {
			searchedRecord = searchedRecord->next;
		}
		// once the insert list is done, this points to the old data
		positionIndex = 0;
	}

	return (searchedPlayer->newRecords + searchedPlayer->numberOfRecords);
}

static int DB_SearchNamePatterns(const db_playeraliases_t *player, const char *pattern)
{
	db_aliases_insertrecord_t *inserts = player->insertlist;
	db_aliases_aliasesrecord_t *old = NULL;
	uint32_t i;

	while( inserts ) {
		if( strstr(inserts->alias.clean_name, pattern) != NULL ) {
			if( search_cache.used_cache == ALIASES_DB_MAXSEARCHCACHE ) {
				return -1;
			}
			search_cache.results[search_cache.used_cache] = player;
			search_cache.used_cache++;
			return 1;
		}
		inserts = inserts->next;
	}

	for( i = 0; i < player->numberOfRecords ; i++ ) {
		old = &player->records[i];
		if( old->actions & ALIASES_ACTION_SKIP ) {
			continue;
		}
		if( strstr(old->alias.clean_name, pattern) != NULL ) {
			if( search_cache.used_cache == ALIASES_DB_MAXSEARCHCACHE ) {
				return -1;
			}
			search_cache.results[search_cache.used_cache] = player;
			search_cache.used_cache++;
			return 1;
		}
	}

	return 0;
}

int G_DB_SearchAliasesNamePattern(const char *pattern)
{
	db_aliases_info_t *info = &aliases_info;
	db_buffered_player_t *buffered = info->buffer;
	db_playeraliases_t *cachedPlayer = NULL;
	db_playeraliases_t *player = NULL;
	uint32_t i, lastIndex;

	if( !info->aliases_inuse ) {
		return -1;
	}

	memset(&search_cache, 0, sizeof(search_cache));

	Q_strncpyz(search_cache.search_pattern, G_DB_SanitizeName(pattern), sizeof(search_cache.search_pattern));

	// through the buffer
	while( buffered ) {
		player = &buffered->player;
		buffered = buffered->next;
		if( player->actions & ALIASES_ACTION_REMOVE ) {
			continue;
		}
		if( DB_SearchNamePatterns(player, search_cache.search_pattern) == -1 ) {
			return -2;
		}
	}

	// through cache
	lastIndex = info->player_count;
	for( i = 0; i < lastIndex ; i++ ) {
		cachedPlayer = &info->players[i];
		if( cachedPlayer->actions & (ALIASES_ACTION_REMOVE | ALIASES_ACTION_SKIP) ) {
			continue;
		}
		if( DB_SearchNamePatterns(cachedPlayer, search_cache.search_pattern) == -1 ) {
			return -2;
		}
	}

	return search_cache.used_cache;
}

static void DB_GetAliasResults(const db_playeraliases_t *player, db_alias_searchresult_t* results)
{
	db_aliases_insertrecord_t *inserts = player->insertlist;
	db_aliases_aliasesrecord_t *old = NULL;
	uint32_t i;

	memset(results, 0, sizeof(db_alias_searchresult_t));

	memcpy(&results->guid, &player->guid, sizeof(player->guid));

	while( inserts ) {
		if( strstr(inserts->alias.clean_name, search_cache.search_pattern) ) {
			if( results->numberOfAliases == ALIASES_DB_MAXALIASES_FORONERESULT ) {
				results->dontFit = qtrue;
				return;
			}
			results->aliases[results->numberOfAliases] = &inserts->alias;
			results->numberOfAliases++;
		}
		results->totalPlayTime += inserts->alias.time_played;
		inserts = inserts->next;
	}

	for( i = 0; i < player->numberOfRecords ; i++ ) {
		old = &player->records[i];
		if( old->actions & ALIASES_ACTION_SKIP ) {
			continue;
		}
		if( strstr(old->alias.clean_name, search_cache.search_pattern) != NULL ) {
			if( results->numberOfAliases == ALIASES_DB_MAXALIASES_FORONERESULT ) {
				results->dontFit = qtrue;
				return;
			}
			results->aliases[results->numberOfAliases] = &old->alias;
			results->numberOfAliases++;
		}
		results->totalPlayTime += old->alias.time_played;
	}
}

db_alias_searchresult_t *G_DB_GetAliasesSearchResult(int position)
{
	static db_alias_searchresult_t result;

	if( position < 0 || (uint32_t)position >= search_cache.used_cache ) {
		return NULL;
	}

	DB_GetAliasResults(search_cache.results[position], &result);

	return &result;
}
