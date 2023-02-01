#include "g_local.h"

#include "g_shrubbotdb.h"
#include "g_db_filehandling.h"
#include "g_db_aliases.h"
#include "silent_acg.h"

//
// Database debug prints
//#define DEBUG_USERSDB 1
/*
	NOTES:
*/

////////////////////////////////////////////////////////////////////////////////
// Database Legacy Data from older versions
//
// Data is only needed for conversions between database versions.

#define DB_USERS_VERSION_01 "SLEnT UDB v0.1\0\0"
#define DB_USERS_VERSION_02 "SLEnT UDB v0.2\0\0"
#define DB_USERS_VERSION_03 "SLEnT UDB v0.3\0\0"

#define DB_USERSEXTRA_VERSION_02 "SLEnT UXDB v0.2\0"
#define DB_USERSEXTRA_VERSION_03 "SLEnT UXDB v0.3\0"

#define SIL_DB_IDENT_LENGTH_OLD 6 // the ident data was changed in 0.6.0

// Structure of the v. 0.1 of the user database
typedef struct g_shrubbot_user_f01_s {
	uint32_t	guidHash;						// for faster searching
	char		guid[SIL_SHRUBBOT_DB_GUIDLEN];	// always use Q_strncmp because there isn't terminating NUL
	char		name[MAX_NAME_LENGTH];			// lowercase last used name of the player for name searches
	char		sanitized_name[MAX_NAME_LENGTH];// we need this for searching
	char		ip[SIL_SHRUBBOT_IPLEN];			// last IP of the user
	int32_t		level;
	char		flags[MAX_SHRUBBOT_FLAGS];
	char		greeting[SIL_DB_GREETING_SIZE];
	char		greeting_sound[SIL_DB_GREETING_SOUND_SIZE];
	// xpsave
	int32_t		time;
	float		skill[SK_NUM_SKILLS];
	uint32_t	kills;
	uint32_t	deaths;
	float		kill_rating;
    float		kill_variance;
	float		rating;
	float		rating_variance;
	int32_t		mutetime;
	uint32_t	activity;		// unused
} g_shrubbot_user_f01_t;  // size: 640 bytes = 160*4 , for 10 000 users ~ 6,1 MB

typedef struct g_shrubbot_user_f02_s {
	uint32_t	guidHash;						// for faster searching
	char		guid[SIL_SHRUBBOT_DB_GUIDLEN];	// remember to always use Q_strncmp because there isn't terminating NUL
	char		name[MAX_NAME_LENGTH];			// lowercase last used name of the player for name searches
	char		sanitized_name[MAX_NAME_LENGTH];// we need this for searching
	char		ip[SIL_SHRUBBOT_IPLEN];			// last IP of the user
	int32_t		level;
	char		flags[MAX_SHRUBBOT_FLAGS];
	// xpsave
	int32_t		time;
	float		skill[SK_NUM_SKILLS];
	uint32_t	kills;
	uint32_t	deaths;
	float		kill_rating;
    float		kill_variance;
	float		rating;
	float		rating_variance;
	int32_t		mutetime;
	uint8_t		ident[SIL_DB_IDENT_LENGTH_OLD];
	uint8_t		ident_flags;
	uint8_t		padding;	// not used for anything, but keeps the alignment with 4
} g_shrubbot_user_f02_t;  // size: 260 bytes for 10 000 users ~ 2 MB

typedef struct g_shrubbot_user_f03_s {
	// for faster searching, both GUIDs are hashed
	uint32_t	guidHash;							// silEnTGUID hash
	uint32_t	pbgHash;							// pb guid hash
	char		pb_guid[SIL_SHRUBBOT_DB_GUIDLEN];	// remember to always use Q_strncmp because there isn't terminating NUL
	char		sil_guid[SIL_SHRUBBOT_DB_GUIDLEN];	// remember to always use Q_strncmp because there isn't terminating NUL
	char		name[MAX_NAME_LENGTH];			// lowercase last used name of the player for name searches
	char		sanitized_name[MAX_NAME_LENGTH];// we need this for searching
	char		ip[SIL_SHRUBBOT_IPLEN];			// last IP of the user
	int32_t		level;
	char		flags[MAX_SHRUBBOT_FLAGS];
	// xpsave
	uint32_t	time;
	float		skill[SK_NUM_SKILLS];
	uint32_t	kills;
	uint32_t	deaths;
	float		kill_rating;
    float		kill_variance;
	float		rating;
	float		rating_variance;
	int32_t		mutetime;
	uint8_t		ident[SIL_DB_IDENT_LENGTH_OLD];
	uint8_t		ident_flags;
	uint8_t		padding;	// not used for anything, but keeps the alignment with 4
	uint32_t	last_xp_save;
} g_shrubbot_user_f03_t;  // size: 300 bytes for 10 000 users ~ 2 MB

typedef struct g_shrubbot_userextra_f02_s {
	char		pb_guid[SIL_SHRUBBOT_DB_GUIDLEN];
	char		greeting[SIL_DB_GREETING_SIZE];
	char		greeting_sound[SIL_DB_GREETING_SOUND_SIZE];
} g_shrubbot_userextra_f02_t;

typedef struct g_shrubbot_userextra_f03_s {
	char		pb_guid[SIL_SHRUBBOT_DB_GUIDLEN];
	char		sil_guid[SIL_SHRUBBOT_DB_GUIDLEN];
	char		greeting[SIL_DB_GREETING_SIZE];
	char		greeting_sound[SIL_DB_GREETING_SOUND_SIZE];
	char		server_key[SIL_DB_KEYSIZE];
	char		client_key[SIL_DB_KEYSIZE];
} g_shrubbot_userextra_f03_t;

////////////////////////////////////////////////////////////////////////////////

#define SIL_SHRUBBOT_DB_ACTION_NONE		0
#define SIL_SHRUBBOT_DB_ACTION_REMOVE	1

// this will indicate the user is in buffer (it's a one way action to be buffered)
#define SIL_SHRUBBOT_DB_BUFFERED		1

// cached database searches, these are flags and can be masked
#define SIL_SHRUBBOT_DB_SEARCHNONE		0
#define SIL_SHRUBBOT_DB_SEARCHNAME		1
#define SIL_SHRUBBOT_DB_SEARCHLEVEL		2
#define SIL_SHRUBBOT_DB_SEARCHIP		4

#define SIL_SHRUBBOT_DB_MAXSEARCHCACHE	256

// so we dont have to go through the file to find the positions 2 times per map
typedef struct {
	g_shrubbot_user_f_t	user;
	const char			*userid;
	const char			*shortPBGUID;
	uint32_t			filePosition;
	uint32_t			buffered;
	int32_t				action;
} g_shrubbot_usercache_t;

typedef struct {
	g_shrubbot_userextra_f_t extras;
	uint32_t			filePosition;
	int32_t				action;
} g_shrubbot_userextras_cache_t;

// User extras are not buffered at all. Append buffer is needed to add
// new extra datas to users not already having them
typedef struct g_shrubbot_userextras_appendbuffer_s {
	g_shrubbot_userextra_f_t		*user;
	int32_t							action;
	struct g_shrubbot_userextras_appendbuffer_s	*next;
} g_shrubbot_userextras_appendbuffer_t;

typedef struct g_shrubbot_buffered_users_s {
	g_shrubbot_usercache_t				*user;
	int32_t								flags;
	float								hits;
	float								team_hits;
	int32_t								axis_time;
	int32_t								allies_time;
	int32_t								kills;
	int32_t								deaths;
	float								total_percent_time;
	float								diff_percent_time;
	int32_t								panzerSelfKills;
	db_alias_t							currentAlias;
	// memory index seems redundant with fileposition, however,
	// the fileposition cannot have negative values so they are not equivalent
	int32_t								memoryIndex;
	struct g_shrubbot_buffered_users_s	*next;
} g_shrubbot_buffered_users_t;

//
// Memory Pooling
// This improves performance and reduces small memory fragmentation from the server process.

#define SIL_DB_BUFFERPOOLSIZE MAX_CLIENTS*2
typedef struct buffer_memory_s {
	uint8_t		pool[sizeof(g_shrubbot_buffered_users_t)*SIL_DB_BUFFERPOOLSIZE];
	size_t		index;
} buffer_memory_t;

// the cached users are huge and also in most times, players are already chached in the big cache
#define SIL_DB_CACHEPOOLSIZE MAX_CLIENTS
typedef struct usercache_memory_s {
	uint8_t		pool[sizeof(g_shrubbot_usercache_t)*SIL_DB_CACHEPOOLSIZE];
	size_t		index;
} usercache_memory_t;

//
// We cache search results becasue it is likely admins will do several searches
// over one period of time. Searches can be done for levels or names.
// All cached data is stored in static data becasue this would surely fragment
// the server memory and the server may need to run over a very long period
// of time.
typedef struct g_shrubbot_searchcache_s {
	uint32_t	search_type;					// level/name
	uint32_t	search_level;					// the stored level
	char		search_pattern[MAX_NAME_LENGTH];// the name pattern
	char		search_ip[20];					// the actual length max is 16, keeping alignment with trailing 0
	uint32_t	used_cache;						// the amount of players in search cache
	uint32_t	usable_results;
	uint32_t	iterator;						// the iterator used with the result fetching
	// value of -1 means the index has been removed
	int32_t		results[SIL_SHRUBBOT_DB_MAXSEARCHCACHE];  // the last results as indexes
} g_shrubbot_searchcache_t;

//
// Fileheader for user database file
// Applicable to database versions:
// - 0.1
// - 0.2
// - 0.3
// - 0.4
typedef struct db_users_fileheader_s {
	char	db_version[DB_USERS_VERSIONSIZE];
	int		records_count;
} db_users_fileheader_t;

typedef struct db_users_info_s {
	int		records_count;
	FILE	*db_file;
	int		extra_count;
	FILE	*extras_file;
	qboolean usable;
	qboolean truncate;
	qboolean optimize;
	qboolean cleanup;
	float	fetch_average;
	int		fetch_count;
	int		lastfetchN;
} db_users_info_t;

////////////////////////////////////////////////////////////////////////////////

static buffer_memory_t		memory_pool;
static usercache_memory_t	cache_pool;
// this handle is used to avoid heap memory allocations when delivering the
// users to outside
static g_shrubbot_user_handle_t handle_out;
static g_shrubbot_buffered_users_t *user_buffer=NULL;
static g_shrubbot_userextras_appendbuffer_t *append_buffer=NULL;
static g_shrubbot_buffered_users_t *user_buffer_last_node=NULL;
static g_shrubbot_buffered_users_t *disconnected_iterator=NULL;
static g_shrubbot_buffered_users_t *buffer_iterator=NULL;
static g_shrubbot_usercache_t *user_cache=NULL;
static g_shrubbot_userextras_cache_t *extras_cache=NULL;
static db_users_info_t	db_users_info;
static uint32_t	cache_iterator;
static uint32_t usercount_onmemory;		// users in cache
static uint32_t extrascount_onmemory;	// user extras in cache
static uint32_t usercount_buffer;		// users in buffer
static uint32_t usercount_onlybuffer;	// users only in buffer, file writes don't reduce this
static g_shrubbot_searchcache_t search_cache;

////////////////////////////////////////////////////////////////////////////////
// memory pooling

// returns the address for a buffered user
static void* DB_AllocBufferUser(void)
{
	uint8_t *addr;

	if(memory_pool.index < SIL_DB_BUFFERPOOLSIZE) {
		// give address from static memory
		addr=&memory_pool.pool[(memory_pool.index*sizeof(g_shrubbot_buffered_users_t))];
		memory_pool.index++;
	} else {
		// fail back system malloc
		addr=(uint8_t*)malloc(sizeof(g_shrubbot_buffered_users_t));
	}
	return addr;
}

static void DB_FreeBufferUser(void *address)
{
	if((uint8_t*)address == NULL) {
		return;
	}
	if((uint8_t*)address >= memory_pool.pool &&
		(uint8_t*)address < &memory_pool.pool[sizeof(g_shrubbot_buffered_users_t)*SIL_DB_BUFFERPOOLSIZE]) {
		// static buffer leave it alone
		return;
	} else {
		// release from heap
		free((g_shrubbot_buffered_users_t*)address);
	}
}

static void* DB_AllocCacheUser(void)
{
	uint8_t *addr;

	if(cache_pool.index < SIL_DB_CACHEPOOLSIZE) {
		// give address from static memory
		addr=&cache_pool.pool[(cache_pool.index*sizeof(g_shrubbot_usercache_t))];
		cache_pool.index++;
	} else {
		// fail back system malloc
		addr=(uint8_t*)malloc(sizeof(g_shrubbot_usercache_t));
	}
	return addr;
}

static void DB_FreeCacheUser(void *address)
{
	if((uint8_t*)address == NULL) {
		return;
	}
	if((uint8_t*)address >= cache_pool.pool &&
		(uint8_t*)address < &cache_pool.pool[sizeof(g_shrubbot_usercache_t)*SIL_DB_CACHEPOOLSIZE]) {
		// static buffer leave it alone
		return;
	} else {
		// release from heap
		free((g_shrubbot_usercache_t*)address);
	}
}

////////////////////////////////////////////////////////////////////////////////
// db file handling

// ensures all database files are closed
static void DB_Files_Close(void)
{
	if( db_users_info.db_file ) {
		G_DB_File_Close(&db_users_info.db_file);
	}
	if( db_users_info.extras_file ) {
		G_DB_File_Close(&db_users_info.extras_file);
	}
}

static int DB_Write_UserDBheader(FILE *handle)
{
	db_users_fileheader_t header;

	if( handle == NULL ) {
		return -1;
	}

	memcpy(header.db_version, DB_USERS_VERSION, DB_USERS_VERSIONSIZE);
	header.records_count=db_users_info.records_count;

	return G_DB_WriteBlockToFile(handle, &header, sizeof(header), 0);
}

static int DB_Write_UserExtrasDBheader(FILE *handle)
{
	db_users_fileheader_t header;

	if( handle == NULL ) {
		return -1;
	}

	memcpy(header.db_version, DB_USERSEXTRA_VERSION, DB_USERS_VERSIONSIZE);
	header.records_count=db_users_info.extra_count;

	return G_DB_WriteBlockToFile(handle, &header, sizeof(header), 0);
}


static void DB_ReadUsersFromDB(void)
{
	int users = db_users_info.records_count;
	int i, pos;

	if(users == 0) {
		G_LogPrintf("  No players in the user database.\n");
		return;
	}

	usercount_onmemory = 0;

	user_cache=(g_shrubbot_usercache_t*)malloc(sizeof(g_shrubbot_usercache_t)*users);
	memset(user_cache,0,sizeof(g_shrubbot_usercache_t)*users);

	// set the file pointer over the header
	G_DB_SetFilePosition(db_users_info.db_file, sizeof(db_users_fileheader_t));

	for(i=0 ; i < users ; i++) {
		pos = G_DB_ReadBlockFromDBFile(db_users_info.db_file, (void*)&user_cache[i].user, sizeof(g_shrubbot_user_f_t), -1);
		if( pos == -1) {
			G_LogPrintf("  Error condition in reading the user database file.\n");
			// error situation, do something here
			break;
		}
		user_cache[i].filePosition = pos;
		user_cache[i].userid = &user_cache[i].user.sil_guid[24];
		user_cache[i].shortPBGUID = &user_cache[i].user.pb_guid[24];
		usercount_onmemory++;
	}
	G_LogPrintf("  %d players cached from the user database.\n", usercount_onmemory);
}

#ifdef DEBUG_USERSDB
static void DB_DebugReadUsersFromDB(void)
{
	int users = db_users_info.records_count;
	int i, pos;

	if(users == 0) {
		G_LogPrintf("  No players in the user database.\n");
		return;
	}

	usercount_onmemory = 0;

	user_cache=(g_shrubbot_usercache_t*)malloc(sizeof(g_shrubbot_usercache_t)*users);
	memset(user_cache,0,sizeof(g_shrubbot_usercache_t)*users);

	// set the file pointer over the header
	G_DB_SetFilePosition(db_users_info.db_file, sizeof(db_users_fileheader_t));

	for(i=0 ; i < users ; i++) {
		pos = G_DB_ReadBlockFromDBFile(db_users_info.db_file, (void*)&user_cache[i].user, sizeof(g_shrubbot_user_f_t), -1);
		if( pos == -1) {
			G_LogPrintf("  Error condition in reading the user database file.\n");
			// error situation, do something here
			break;
		}
		user_cache[i].filePosition = pos;
		user_cache[i].userid = &user_cache[i].user.sil_guid[24];
		user_cache[i].shortPBGUID = &user_cache[i].user.pb_guid[24];
		G_LogPrintf("  (%d) Read player '%s' SGUID(%.32s) PBGUID(%.32s).\n", (i+1), user_cache[i].user.name, user_cache[i].user.sil_guid, user_cache[i].user.pb_guid);
		usercount_onmemory++;
	}
	if( G_DB_GetRemainingByteCount(db_users_info.db_file) > 0 ) {
		G_LogPrintf("  File has unexpected additional data (%d bytes).\n", G_DB_GetRemainingByteCount(db_users_info.db_file));
	}
	G_LogPrintf("  %d players cached from the user database.\n", usercount_onmemory);
}
#endif

static void DB_ReadExtrasFromDB(void)
{
	int users = db_users_info.extra_count;
	int i, pos;

	if( users == 0 ) {
		G_LogPrintf("  No additional user records in the user database.\n");
		return;
	}

	extrascount_onmemory = 0;

	extras_cache=(g_shrubbot_userextras_cache_t*)malloc(sizeof(g_shrubbot_userextras_cache_t)*users);
	memset(extras_cache,0,sizeof(g_shrubbot_userextras_cache_t)*users);

	// set the file pointer over the header
	G_DB_SetFilePosition(db_users_info.extras_file, sizeof(db_users_fileheader_t));

	for(i=0 ; i < users ; i++) {
		pos = G_DB_ReadBlockFromDBFile(db_users_info.extras_file, (void*)&extras_cache[i].extras, sizeof(g_shrubbot_userextra_f_t), -1);
		if( pos == -1 ) {
			G_LogPrintf("  Error condition in reading the user database file.\n");
			// error situation, do something here
			break;
		}
		extras_cache[i].filePosition = pos;
		extrascount_onmemory++;
	}
	G_LogPrintf("  %d records cached from the additional user info files.\n", extrascount_onmemory);
}

static void DB_ReadUserFromFile(g_shrubbot_usercache_t *user)
{
	g_shrubbot_user_f_t *usr=&user->user;

	G_DB_ReadBlockFromDBFile(db_users_info.db_file, (void*)usr, sizeof(g_shrubbot_user_f_t), user->filePosition);
}

static void DB_WriteUserToDB(g_shrubbot_usercache_t *user, int *newUsers)
{
	// writing single user into db
	if(user->filePosition) {
		// loaded from db , can't be zero because of the header
		G_DB_WriteBlockToFile(db_users_info.db_file, (void*)&user->user, sizeof(g_shrubbot_user_f_t), user->filePosition);
	} else {
		// new record at the end of the file
		G_DB_SetFilePosition(db_users_info.db_file, -1);
		// filePosition gets updated but not the memoryindex because it's not in memory cache
		user->filePosition = G_DB_WriteBlockToFile(db_users_info.db_file, (void*)&user->user, sizeof(g_shrubbot_user_f_t), -1);
		db_users_info.records_count++;
		*newUsers+=1;
	}
}

static void DB_WriteUsersToDB(qboolean free_memory)
{
	g_shrubbot_buffered_users_t *users=user_buffer;
	g_shrubbot_buffered_users_t *temp=NULL;
	int newUsers=0;

	G_DB_File_Open(&db_users_info.db_file, DB_USERS_FILENAME, DB_FILEMODE_UPDATE);

	if( !db_users_info.db_file ) {
		G_LogPrintf("  Error: Failed to open userdb.db. File will not be updated.\n");
		return;
	}

	while( users ) {
		temp = users;
		// append some final data to old values, client needs to have had full init for this
		if( temp->flags & SIL_DBUSERFLAG_FULLINIT ) {
			temp->user->user.kills += temp->kills;
			temp->user->user.deaths += temp->deaths;
		}
		DB_WriteUserToDB(temp->user, &newUsers);
		users = users->next;
		if( free_memory ) {
			if( temp->memoryIndex == -1 ) {
				// not freeing memory that was not explicitly made for the buffer
				// this might look weird, so, freeing the memory made for the non cached user
				// i.e. memoryIndex is the index in the cache but the function name here is little misleading
				DB_FreeCacheUser(temp->user);
			}
			DB_FreeBufferUser(temp);
			user_buffer = users;
			usercount_buffer--;
		}
	}
	// updating the records amount in header
	DB_Write_UserDBheader(db_users_info.db_file);
	G_DB_File_Close(&db_users_info.db_file);
}

static qboolean DB_ExtrasRequireFileWrite( g_shrubbot_userextra_f_t *extras )
{
	if( extras->server_key[0] && extras->client_key[0] ) {
		return qtrue;
	}
	if( extras->greeting[0] || extras->greeting_sound[0] ) {
		return qtrue;
	}
	if( extras->mute_reason[0] || extras->muted_by[0] ) {
		return qtrue;
	}
	return qfalse;
}

// Always rewrites the extra data
static void DB_WriteExtrasToDB(qboolean free_memory)
{
	g_shrubbot_userextras_appendbuffer_t *users=append_buffer;
	g_shrubbot_userextras_appendbuffer_t *temp=NULL;
	unsigned int i;

	db_users_info.extra_count = 0;

	G_DB_File_Open(&db_users_info.extras_file, DB_USERSEXTRA_FILENAME, DB_FILEMODE_TRUNCATE);

	if( !db_users_info.extras_file ) {
		G_LogPrintf("  Error: Could not open userxdb.db file. File will not be written.\n");
		return;
	}

	DB_Write_UserExtrasDBheader(db_users_info.extras_file);
	// writing cache first
	for(i=0; i < extrascount_onmemory ; i++) {
		if(extras_cache[i].action & SIL_SHRUBBOT_DB_ACTION_REMOVE) {
			continue;
		}
		if( !DB_ExtrasRequireFileWrite(&extras_cache[i].extras) ) {
			continue;
		}
		G_DB_WriteBlockToFile(db_users_info.extras_file, (void*)&extras_cache[i], sizeof(g_shrubbot_userextra_f_t), -1);
		db_users_info.extra_count++;
	}
	// new users then at at the end of file
	while(users) {
		temp = users;

		if( !(users->action & SIL_SHRUBBOT_DB_ACTION_REMOVE) && DB_ExtrasRequireFileWrite(users->user) ) {
			// not saving users that are to be removed
			// also, automatically leave out users that don't have data to store
			G_DB_WriteBlockToFile(db_users_info.extras_file, users->user, sizeof(g_shrubbot_userextra_f_t), -1);
			db_users_info.extra_count++;
		}
		users = users->next;
		if(free_memory) {
			free(temp->user);
			free(temp);
		}
	}

	// updating the records amount in header
	DB_Write_UserExtrasDBheader(db_users_info.extras_file);
	G_DB_File_Close(&db_users_info.extras_file);
}

//
// Will write all the users into a file. If the server operation is to continue
// normally after this function, the users must be recached and buffer completely cleaned.
// Some data would otherwise get distorted.
static void DB_WriteUsersTruncFile(void)
{
	g_shrubbot_buffered_users_t *users_b=user_buffer;
	uint32_t					users_c;
	uint32_t					i;
	int newUsers=0;

	db_users_info.records_count=0;

	// truncation is not enough, in some systems the file remains the size though it's empty
	//G_DB_DeleteFile(DB_USERS_FILENAME); // this was an old programming error, it is not actually needed

	// open file as truncated
	G_DB_File_Open(&db_users_info.db_file, DB_USERS_FILENAME, DB_FILEMODE_TRUNCATE);

	if( !db_users_info.db_file ) {
		G_LogPrintf("  Error: Failed to open userdb.db. File will not be updated.\n");
		return;
	}

	// initial file header
	DB_Write_UserDBheader(db_users_info.db_file);

	// write all the buffered users that are not cached
	while(users_b) {
		// updating the required values for all buffered users
		if( users_b->flags & SIL_DBUSERFLAG_FULLINIT ) {
			users_b->user->user.kills+=users_b->kills;
			users_b->user->user.deaths+=users_b->deaths;
		}
		// but writing only those that are not cached (less comparisons in the next loop)
		if( (users_b->memoryIndex == -1) && (users_b->user->action != SIL_SHRUBBOT_DB_ACTION_REMOVE) ) {
			// manually making sure it will not be written to the old known position in the file
			// some shrubbot commands can possibly write the user to the file without recaching the db
			users_b->user->filePosition = 0;
			DB_WriteUserToDB(users_b->user, &newUsers);
		}
		users_b=users_b->next;
	}

	// write the on memory cache
	users_c = usercount_onmemory;
	for(i=0; i<users_c ;i++) {
		if(user_cache[i].action==SIL_SHRUBBOT_DB_ACTION_REMOVE) {
			continue; // just skip
		}
		if( G_DB_WriteBlockToFile(db_users_info.db_file, (void*)&user_cache[i], sizeof(g_shrubbot_user_f_t), -1) < 0 ) {
			// error, do something
			break;
		}
		db_users_info.records_count++;
	}

	// rewrite the header with correct user amount
	DB_Write_UserDBheader(db_users_info.db_file);

	// all done
	G_DB_File_Close(&db_users_info.db_file);
}

static g_shrubbot_buffered_users_t* DB_BufferedUserNode(const uint32_t guidHash, const char *guid)
{
	g_shrubbot_buffered_users_t *users = user_buffer;
	g_shrubbot_user_f_t *user;

	while( users ) {
		user = &users->user->user;
		if( (user->ident_flags & SIL_DBGUID_VALID) && guidHash == user->guidHash && !Q_strncmp(user->sil_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
			return users;
		}
		users = users->next;
	}
	return NULL;
}

static g_shrubbot_buffered_users_t* DB_BufferedUserNodePB(const uint32_t guidHash, const char *guid)
{
	g_shrubbot_buffered_users_t *users = user_buffer;

	while( users ) {
		if( guidHash == users->user->user.pbgHash && !Q_strncmp(users->user->user.pb_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
			return users;
		}
		users = users->next;
	}
	return NULL;
}

//
// Function adds a user to the buffer.
// If the user exists in the on memory cache, the address to the memory cache
// is used as such.
// If user is not in the memory cache, a new instance is created.
static g_shrubbot_buffered_users_t* DB_BufferUserNode(g_shrubbot_usercache_t *user, int32_t index)
{
	// function creates a new instance of the user type stack variables are safe when calling this function
	g_shrubbot_buffered_users_t *users=user_buffer;

	if(!users) {
		users=(g_shrubbot_buffered_users_t*)DB_AllocBufferUser();
		//users=(g_shrubbot_buffered_users_t*)malloc(sizeof(g_shrubbot_buffered_users_t));
		user_buffer=users; // first node
	} else {
		users=user_buffer_last_node;
		//users->next=(g_shrubbot_buffered_users_t*)malloc(sizeof(g_shrubbot_buffered_users_t));
		users->next=(g_shrubbot_buffered_users_t*)DB_AllocBufferUser();
		users=users->next;
	}
	users->next=NULL;
	user_buffer_last_node=users;

	if(index!=-1) {
		// copy the address don't dublicate
		users->user=user;
	} else {
		// whole new user, copy data and set pointers to new data
		users->user=(g_shrubbot_usercache_t*)DB_AllocCacheUser();
		//users->user=(g_shrubbot_usercache_t*)malloc(sizeof(g_shrubbot_usercache_t));
		memcpy(&users->user->user,&user->user,sizeof(g_shrubbot_user_f_t));
		usercount_onlybuffer++;
		users->user->userid = &users->user->user.sil_guid[24];
		users->user->shortPBGUID = &users->user->user.pb_guid[24];
	}
	users->user->filePosition=user->filePosition;
	users->user->action=user->action;
	users->user->buffered=SIL_SHRUBBOT_DB_BUFFERED;
	users->memoryIndex=index;
	// data that is in the stored data but that needs special buffering
	users->kills=0;
	users->deaths=0;
	// zero set level specific data
	users->hits=0;
	users->team_hits=0;
	users->allies_time=0;
	users->axis_time=0;
	users->diff_percent_time=0;
	users->total_percent_time=0;
	users->panzerSelfKills = 0;
	users->flags=0;

	usercount_buffer++;

	return users;
}

static g_shrubbot_buffered_users_t* DB_UnbufferedUserNodePB(const uint32_t guidHash, const char *guid)
{
	int32_t users = usercount_onmemory;
	int32_t i;

	for(i=0; i < users ;i++) {
		if( guidHash == user_cache[i].user.pbgHash && !Q_strncmp(user_cache[i].user.pb_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
			// put the user into buffer
			db_users_info.lastfetchN += (i+1);
			return DB_BufferUserNode(&user_cache[i], i);
		}
	}
	db_users_info.lastfetchN += (i+1);

	return NULL;
}

static g_shrubbot_buffered_users_t* DB_UnbufferedUserNodePBNoHash(const char *guid)
{
	int32_t users = usercount_onmemory;
	int32_t i;

	for(i=0; i < users ;i++) {
		if( !Q_strncmp(user_cache[i].user.pb_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
			// put the user into buffer
			return DB_BufferUserNode(&user_cache[i], i);
		}
	}

	return NULL;
}

static g_shrubbot_buffered_users_t* DB_UnbufferedUserNode(const uint32_t guidHash, const char *guid)
{
	int32_t users = usercount_onmemory;
	g_shrubbot_user_f_t *user;
	int32_t i;

	for(i=0; i < users ;i++) {
		user = &user_cache[i].user;
		if( (user->ident_flags & SIL_DBGUID_VALID) && guidHash == user->guidHash && !Q_strncmp(user->sil_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
			// put the user into buffer
			db_users_info.lastfetchN += (i+1);
			return DB_BufferUserNode(&user_cache[i], i);
		}
	}
	db_users_info.lastfetchN += (i+1);

	return NULL;
}

static void DB_DestroyBuffers(void)
{
	g_shrubbot_buffered_users_t *users = user_buffer;
	g_shrubbot_buffered_users_t *temp;
	g_shrubbot_userextras_appendbuffer_t *appends = append_buffer;
	g_shrubbot_userextras_appendbuffer_t *temp_extra;

	while(users) {
		temp=users;
		users=users->next;
		if(temp->memoryIndex==-1) {
			// only free users that aren't in the big memory cache
			DB_FreeCacheUser(temp->user);
		}
		DB_FreeBufferUser(temp);
		user_buffer=users;
	}

	user_buffer=NULL;
	user_buffer_last_node=NULL;
	usercount_buffer=0;

	// extra user extra
	while(appends) {
		temp_extra = appends;
		appends = appends->next;
		if(temp_extra->user) {
			free(temp_extra->user);
		}
		free(temp_extra);
	}
	append_buffer = NULL;
}

static void DB_DestroyCaches(void)
{
	if(user_cache) {
		free(user_cache);
		user_cache=NULL;
	}
	usercount_onmemory=0;

	if(extras_cache) {
		free(extras_cache);
		extras_cache=NULL;
	}
	extrascount_onmemory=0;
}

static g_shrubbot_buffered_users_t* DB_NewUserNode(const uint32_t guidHash, const char* guid)
{
	g_shrubbot_buffered_users_t *node = NULL;
	g_shrubbot_usercache_t		newuser;

	newuser.action=SIL_SHRUBBOT_DB_ACTION_NONE;
	newuser.filePosition=0;
	memset(&newuser.user, 0, sizeof(g_shrubbot_user_f_t));
	memcpy(newuser.user.sil_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN);
	newuser.user.rating_variance = SIGMA2_THETA;
	newuser.user.kill_variance = SIGMA2_DELTA;
	newuser.user.guidHash = guidHash;
	node = DB_BufferUserNode(&newuser, -1);

	return node;
}

static g_shrubbot_buffered_users_t* DB_GetUserNode(const uint32_t guidHash, const char* guid)
{
	g_shrubbot_buffered_users_t *node=NULL;

	node = DB_BufferedUserNode(guidHash, guid);

	if( node == NULL ) {
		// searching the on memory cache and adding the user to buffer if found
		node = DB_UnbufferedUserNode(guidHash, guid);
	}

	return node;
}

static g_shrubbot_user_f_t* DB_GetUserNodeWithoutBuffering(const uint32_t guidHash, const char* guid)
{
	g_shrubbot_user_f_t *user = NULL;
	g_shrubbot_buffered_users_t *node = NULL;

	node = DB_BufferedUserNode(guidHash, guid);

	if( node == NULL ) {
		// searching the on memory cache and adding the user to buffer if found
		int32_t users = usercount_onmemory;
		int32_t i;

		for(i=0; i < users ;i++) {
			user = &user_cache[i].user;
			if( (user->ident_flags & SIL_DBGUID_VALID) && guidHash == user->guidHash && !Q_strncmp(user->sil_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
				break;
			}
		}

		if( i == users ) {
			return NULL;
		}
	} else {
		user = &node->user->user;
	}

	return user;
}

static g_shrubbot_buffered_users_t* DB_GetUserNodePB(const uint32_t guidHash, const char* guid)
{
	g_shrubbot_buffered_users_t *node = NULL;

	node = DB_BufferedUserNodePB(guidHash, guid);

	if( node == NULL ) {
		// searching the on memory cache and adding the user to buffer if found
		node = DB_UnbufferedUserNodePB(guidHash, guid);
	}

	return node;
}

static g_shrubbot_buffered_users_t* DB_GetUserNodePBNoHash(const char* guid)
{
	g_shrubbot_buffered_users_t *node = NULL;

	if( node == NULL ) {
		// searching the on memory cache and adding the user to buffer if found
		node = DB_UnbufferedUserNodePBNoHash(guid);
	}

	return node;
}

/**
	Function checks that the extras in the DB have a purpose and sets them to be removed if not.
	Only cache needs to be checked.
*/
static void DB_ExtrasCleanup(void)
{
	g_shrubbot_user_f_t	*user;
	uint32_t guidHash;
	uint32_t i;

	for(i=0; i < extrascount_onmemory ; i++) {
		if(extras_cache[i].action & SIL_SHRUBBOT_DB_ACTION_REMOVE) {
			continue;
		}
		// check if the player is found from userdb
		/*
		trusting that the userxdb.db is well written, i.e. the guid is uppercase already
		if it wasn't, the record would be unusable anyway
		for(j=0; j < 32 && extras_cache[i].extras.sil_guid[j] ;j++) {
			guid[j]=toupper(extras_cache[i].extras.sil_guid[j]);
		}
		if(i!=32) { return; }
		*/
		guidHash = BG_hashword((const uint32_t*)extras_cache[i].extras.sil_guid, 8, 0);

		user = DB_GetUserNodeWithoutBuffering(guidHash, extras_cache[i].extras.sil_guid);

		if( !user ) {
			extras_cache[i].action |= SIL_SHRUBBOT_DB_ACTION_REMOVE;
			continue;
		}
		// check if the player is muted and ensure empty mute data if not
		if( user->mutetime == 0 ) {
			extras_cache[i].extras.muted_by[0] = '\0';
			extras_cache[i].extras.mute_reason[0] = '\0';
		} else if( user->mutetime > 0 && user->mutetime < level.realtime ) {
			// clean up expired mutes
			user->mutetime = 0;
			extras_cache[i].extras.muted_by[0] = '\0';
			extras_cache[i].extras.mute_reason[0] = '\0';
		}
	}
}

static void DB_FillHandle(g_shrubbot_buffered_users_t *node, g_shrubbot_user_handle_t *handle)
{
	memset(handle, 0, sizeof(g_shrubbot_user_handle_t));
	handle->user = &node->user->user;
	handle->hits = node->hits;
	handle->team_hits = node->team_hits;
	handle->allies_time = node->allies_time;
	handle->axis_time = node->axis_time;
	//handle->kills=0; // these values go only to db not out
	//handle->deaths=0;
	handle->flags = node->flags;
	handle->diff_percent_time = node->diff_percent_time;
	handle->total_percent_time = node->total_percent_time;
	handle->userid = node->user->userid;
	handle->shortPBGUID = node->user->shortPBGUID;
	handle->node = (void*)node;
}

static void DB_FillHandleFromFileRecord(g_shrubbot_user_f_t *node, g_shrubbot_user_handle_t *handle)
{
	memset(handle, 0, sizeof(g_shrubbot_user_handle_t));
	handle->user = node;
	handle->flags = SIL_DBUSERFLAG_CACHED;
	handle->userid = &node->sil_guid[24];
	handle->shortPBGUID = &node->pb_guid[24];
	handle->node=(void*)node;
}

static qboolean DB_IsInBuffer(uint32_t index)
{
	if(user_cache[index].buffered==SIL_SHRUBBOT_DB_BUFFERED) {
		return qtrue;
	}
	return qfalse;
}

static qboolean DB_IsRemoved(uint32_t index)
{
	if(user_cache[index].action==SIL_SHRUBBOT_DB_ACTION_REMOVE) {
		return qtrue;
	}
	return qfalse;
}


////////////////////////////////////////////////////////////////////////
// DB_UserDB_Close
//
// Function closes initialised database. Function is safe to be called
// anytime. Function doesn't store any data to the file.
// After function is called, the database initialisation is the only
// way to get access to the database again.
//
static void DB_UserDB_Close(void)
{
	if(db_users_info.usable==qfalse) {
		return;
	}

	DB_Files_Close();

	db_users_info.usable=qfalse;
}

//
// This algorithm must match with the XP save algorithms
static int32_t DB_Get_UserMaxAge(void) {
	int32_t result = g_dbUserMaxAge.integer;

	if (*g_dbUserMaxAge.string) {
		switch(g_dbUserMaxAge.string[strlen(g_dbUserMaxAge.string) - 1]) {
			case 'O':
			case 'o':
				result *= 4;

			case 'W':
			case 'w':
				result *= 7;

			case 'D':
			case 'd':
				result *= 24;

			case 'H':
			case 'h':
				result *= 60;

			case 'M':
			case 'm':
				result *= 60;

				break;
		}
	}

	return result;
}

char* G_DB_SanitizeName(const char* name)
{
	static char name_buf[MAX_NAME_LENGTH];
	char	*point=&name_buf[0];
	int		i;

	for(i=0; *name && i < (MAX_NAME_LENGTH - 1); i++) {
		if(*name=='^') {
			name++;
			if(*(name)!='^') {
				name++;
				continue;
			}
			continue;
		}
		*point++=tolower(*name++);
	}
	*point='\0';
	return &name_buf[0];
}

////////////////////////////////////////////////////////////////////////////////
// User Extra data handling

static g_shrubbot_userextra_f_t* DB_FindUserExtras(const g_shrubbot_user_f_t *user)
{
	// First look from append buffer
	g_shrubbot_userextras_appendbuffer_t *appends=append_buffer;
	uint32_t count = extrascount_onmemory;
	uint32_t i;
	const char *sil_guid = user->sil_guid;
	const char *pb_guid = user->pb_guid;

	while(appends) {
		if( sil_guid[0] ) {
			if(!Q_stricmpn(appends->user->sil_guid, sil_guid, SIL_SHRUBBOT_DB_GUIDLEN)) {
				return appends->user;
			}
		}
		if( pb_guid[0] ) {
			if(!Q_stricmpn(appends->user->pb_guid, pb_guid, SIL_SHRUBBOT_DB_GUIDLEN)) {
				return appends->user;
			}
		}
		appends=appends->next;
	}
	// Loop the cache
	for(i=0; i < count ; i++) {
		if( sil_guid[0] ) {
			if( !Q_stricmpn(extras_cache[i].extras.sil_guid, sil_guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
				return &extras_cache[i].extras;
			}
		}
		if( pb_guid[0] ) {
			if( !Q_stricmpn(extras_cache[i].extras.pb_guid, pb_guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
				return &extras_cache[i].extras;
			}
		}
	}

	return NULL;
}

static g_shrubbot_userextras_cache_t* DB_FindExtrasCacheData(const char* guid)
{
	uint32_t count=db_users_info.extra_count;
	uint32_t i;

	for(i=0; i < count ; i++) {
		if(!Q_stricmpn(extras_cache[i].extras.sil_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN)) {
			return &extras_cache[i];
		}
	}
	return NULL;
}

static g_shrubbot_userextras_cache_t* DB_FindExtrasCacheDataPB(const char* guid)
{
	uint32_t count=db_users_info.extra_count;
	uint32_t i;

	for(i=0; i < count ; i++) {
		if(!Q_stricmpn(extras_cache[i].extras.pb_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN)) {
			return &extras_cache[i];
		}
	}
	return NULL;
}

static int DB_AppendNewExtras(g_shrubbot_userextra_f_t *extras)
{
	g_shrubbot_userextras_appendbuffer_t *appends;

	if( !extras ) {
		return -1;
	}

	if(append_buffer == NULL) {
		append_buffer = (g_shrubbot_userextras_appendbuffer_t*)malloc(sizeof(g_shrubbot_userextras_appendbuffer_t));
		appends = append_buffer;
	} else {
		appends = append_buffer;
		while(appends) {
			appends = appends->next;
		}
		appends = (g_shrubbot_userextras_appendbuffer_t*)malloc(sizeof(g_shrubbot_userextras_appendbuffer_t));
	}

	if(appends == NULL) {
		return -1;
	}
	appends->user=extras;
	appends->next=NULL;

	return 0;
}

// wrapper only
const g_shrubbot_userextra_f_t* G_DB_GetUserExtras(const g_shrubbot_user_handle_t *handle)
{
	return DB_FindUserExtras(handle->user);
}

static g_shrubbot_userextra_f_t* DB_CreateUserExtras(const g_shrubbot_user_f_t *user)
{
	g_shrubbot_userextra_f_t *userExt = NULL;

	userExt = (g_shrubbot_userextra_f_t*)malloc(sizeof(g_shrubbot_userextra_f_t));

	if( !userExt ) {
		return NULL;
	}

	memset(userExt, 0, sizeof(g_shrubbot_userextra_f_t));
	memcpy(userExt->sil_guid, user->sil_guid, sizeof(userExt->sil_guid));
	memcpy(userExt->pb_guid, user->pb_guid, sizeof(userExt->pb_guid));

	return userExt;
}

static void DB_DatabaseCleanUp(void)
{
	uint32_t guidHash;
	uint32_t pb_guidHash;
	uint32_t identFlags;
	int32_t users = usercount_onmemory;
	int32_t i, j, badHashes = 0, duplicates = 0, unlinkables = 0;
	qboolean linkable = qfalse;
	g_shrubbot_usercache_t *user;
	g_shrubbot_usercache_t *oldUser;

	G_LogPrintf("*=====PERFORMING USER DATABASE CLEAN UP\n");

	for( j = 0; j < users; j++ ) {
		user = &user_cache[j];

		if( user->action & SIL_SHRUBBOT_DB_ACTION_REMOVE ) {
			continue;
		}

		guidHash = 0;
		pb_guidHash = 0;

		// check GUIDs and fix hashes if necessary
		if( G_CheckGUID(user->user.sil_guid, qfalse) ) {
			guidHash = BG_hashword((const uint32_t*)user->user.sil_guid, 8, 0);
			if( user->user.guidHash != guidHash ) {
				user->user.guidHash = guidHash;
				badHashes++;
			}
			linkable = qtrue;
		} else {
			user->user.guidHash = 0;
			user->user.ident_flags &= ~SIL_DBGUID_VALID;
		}

		if( G_CheckGUID(user->user.pb_guid, qfalse) ) {
			pb_guidHash = BG_hashword((const uint32_t*)user->user.pb_guid, 8, 0);
			if( user->user.pbgHash != pb_guidHash ) {
				user->user.pbgHash = pb_guidHash;
				badHashes++;
			}
			linkable = qtrue;
		} else {
			user->user.pbgHash = 0;
		}

		if( linkable == qfalse ) {
			user->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			unlinkables++;
			continue;
		}

		// check for dublicated, doing full loop here, so that the latest one can be used as the valid one
		// SIL_DBGUID_VALID is not set with older silent GUIDs
		identFlags = (user->user.ident_flags & SIL_DBGUID_VALID);
		oldUser = user;
		for(i=0; i < users ;i++) {
			if( &user_cache[i] == oldUser ) {
				continue;
			}
			if( user_cache[i].action & SIL_SHRUBBOT_DB_ACTION_REMOVE ) {
				continue;
			}
			// !readadmins can import admins without either GUID
			if( guidHash && guidHash == user_cache[i].user.guidHash && ((user_cache[i].user.ident_flags & SIL_DBGUID_VALID) == identFlags)
				&& !Q_strncmp(user_cache[i].user.sil_guid, user->user.sil_guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
				// older gets removed
				if( user_cache[i].user.time < user->user.time ) {
					user_cache[i].action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
				} else {
					oldUser->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
					oldUser = &user_cache[i];
				}
				duplicates++;
			} else if( pb_guidHash && user_cache[i].user.guidHash == 0 ) {
				// concerned about punkbuster guid hash only if the record is not linkable through silent guid
				if( user_cache[i].user.pbgHash == pb_guidHash && !Q_strncmp(user_cache[i].user.pb_guid, user->user.pb_guid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
					// older gets removed
					if( user_cache[i].user.time < user->user.time ) {
						user_cache[i].action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
					} else {
						oldUser->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
						oldUser = &user_cache[i];
					}
					duplicates++;
				}
			}
		}
	}
	G_LogPrintf("  * Main database:\n");
	if( badHashes ) {
		G_LogPrintf("  Found and fixed %d GUID related errors.\n", badHashes);
	}
	if( duplicates ) {
		G_LogPrintf("  Issued delete to %d duplicated records.\n", duplicates);
	}
	if( unlinkables ) {
		G_LogPrintf("  Issued delete to %d unusable records.\n", unlinkables);
	}
	if( !badHashes && !duplicates && !unlinkables ) {
		G_LogPrintf("  No bad records found from the database.\n");
	} else {
		// mark the file for full rewrite
		db_users_info.truncate = qtrue;
	}

	G_DB_CleanUpAliases();

	G_LogPrintf("*=====USER DATABASE CLEAN UP DONE\n");
}

static void DB_WriteUsersOptimizeFile(void)
{
	g_shrubbot_buffered_users_t *users_b=user_buffer;
	g_shrubbot_userextras_cache_t *extras;
	g_shrubbot_usercache_t		*user = NULL;
	uint32_t					users_c;
	uint32_t					i,j, lastseen;
	int newUsers=0;

	db_users_info.records_count=0;

	// truncation is not enough, in some systems the file remains the size though it's empty
	G_DB_DeleteFile(DB_USERS_FILENAME);

	// open file as truncated
	G_DB_File_Open(&db_users_info.db_file, DB_USERS_FILENAME, DB_FILEMODE_TRUNCATE);

	// initial file header
	DB_Write_UserDBheader(db_users_info.db_file);

	// write all the buffered users that are not cached
	while( users_b ) {
		// updating the required values for all buffered users
		if(users_b->flags & SIL_DBUSERFLAG_FULLINIT) {
			users_b->user->user.kills+=users_b->kills;
			users_b->user->user.deaths+=users_b->deaths;
		}
		// but writing only those that are not cached (less comparisons in the next loop)
		if( (users_b->memoryIndex == -1) && (users_b->user->action != SIL_SHRUBBOT_DB_ACTION_REMOVE) ) {
			// manually making sure it will not be written to the old known position in the file
			// some shrubbot commands can possibly write the user to the file without recaching the db
			users_b->user->filePosition = 0;
			DB_WriteUserToDB(users_b->user, &newUsers);
		}
		users_b=users_b->next;
	}

	// write the on memory cache
	users_c = usercount_onmemory;

	// first clean unlinkables
	for(i=0; i<users_c ;i++) {
		if( !user_cache[i].user.pbgHash && !(user_cache[i].user.ident_flags & SIL_DBGUID_VALID) ) {
			user_cache[i].action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			extras = DB_FindExtrasCacheData(user_cache[i].user.sil_guid);
			if( extras ) {
				extras->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			}
		}
	}
	// loop the amount of users, loop to find the latest last seen, insert into file
	for(i=0; i<users_c ;i++) {
		lastseen = 0;
		user = NULL;
		for(j=0; j < users_c ; j++) {
			if(user_cache[j].action==SIL_SHRUBBOT_DB_ACTION_REMOVE) {
				continue; // just skip
			}
			if( user_cache[j].user.time > lastseen ) {
				lastseen = user_cache[j].user.time;
				user = &user_cache[j];
			}
		}
		if( user ) {
			if( G_DB_WriteBlockToFile(db_users_info.db_file, (void*)user, sizeof(g_shrubbot_user_f_t), -1) < 0 ) {
				// error, do something
				break;
			}
			db_users_info.records_count++;
			user->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
		}
	}
	// another loop to write all those who have never been on the server, i.e. players added with !readadmins
	for(i=0; i<users_c ;i++) {
		if(user_cache[i].action==SIL_SHRUBBOT_DB_ACTION_REMOVE) {
			continue; // just skip
		}
		if( G_DB_WriteBlockToFile(db_users_info.db_file, (void*)&user_cache[i], sizeof(g_shrubbot_user_f_t), -1) < 0 ) {
			// error, do something
			break;
		}
		db_users_info.records_count++;
	}

	// rewrite the header with correct useramount
	DB_Write_UserDBheader(db_users_info.db_file);

	// all done, the data in memory is non reachable and cleanup is mandatory
	G_DB_File_Close(&db_users_info.db_file);
}

int G_DB_SetUserGreeting(const g_shrubbot_user_handle_t *handle, const char *greeting)
{
	g_shrubbot_userextra_f_t *userExt=NULL;

	userExt = DB_FindUserExtras(handle->user);

	if( !userExt ) {
		userExt = DB_CreateUserExtras(handle->user);
		DB_AppendNewExtras(userExt);
	}

	if( userExt ) {
		if(greeting[0] == '\0') {
			memset(userExt->greeting, 0, sizeof(userExt->greeting));
		} else {
			Q_strncpyz(userExt->greeting, greeting, sizeof(userExt->greeting));
		}
	} else {
		return -1;
	}

	return 0;
}

int G_DB_SetUserGreetingSound(const g_shrubbot_user_handle_t *handle, const char *greeting_sound)
{
	g_shrubbot_userextra_f_t *userExt = NULL;

	userExt = DB_FindUserExtras(handle->user);

	if( !userExt ) {
		userExt = DB_CreateUserExtras(handle->user);
		DB_AppendNewExtras(userExt);
	}

	if( userExt ) {
		if(greeting_sound[0] == '\0') {
			memset(userExt->greeting_sound, 0, sizeof(userExt->greeting_sound));
		} else {
			Q_strncpyz(userExt->greeting_sound, greeting_sound, sizeof(userExt->greeting_sound));
		}
	} else {
		return -1;
	}

	return 0;
}

int G_DB_SetUserKeys(const gentity_t *ent, const char *serverKey, const char *clientKey)
{
	g_shrubbot_userextra_f_t *userExt = NULL;
	const g_shrubbot_user_f_t *user;

	if( !g_clientSInfos[ent-g_entities].userData ) {
		return -1;
	}

	user = g_clientSInfos[ent-g_entities].userData;
	userExt = (g_shrubbot_userextra_f_t*) g_clientSInfos[ent-g_entities].extraData;

	if( !userExt ) {
		userExt = DB_CreateUserExtras(user);
		if( DB_AppendNewExtras(userExt) == 0 ) {
			// set the appropriate pointers for the game, in case the admin decides to reconnect during the map the keys are found
			g_clientSInfos[ent-g_entities].extraData = userExt;
		}
	}

	if( userExt ) {
		memcpy(userExt->server_key, serverKey, sizeof(userExt->server_key));
		memcpy(userExt->client_key, clientKey, sizeof(userExt->client_key));
	} else {
		return -1;
	}

	return 0;
}

qboolean G_DB_UserHasKeys(const gentity_t *ent)
{
	const g_shrubbot_userextra_f_t *userExt = g_clientSInfos[ent-g_entities].extraData;

	if( !userExt ) {
		return qfalse;
	}

	if( userExt->server_key[0] && userExt->client_key[0] ) {
		return qtrue;
	}

	return qfalse;
}

////////////////////////////////////////////////////////////////////////////////

static int DB_CreateExtrasDB(void)
{
	db_users_info_t	*info=&db_users_info;

	G_DB_File_Open(&info->extras_file, DB_USERSEXTRA_FILENAME, DB_FILEMODE_TRUNCATE);

	if(!info->extras_file) {
		G_LogPrintf("  Failed to create database file.\n");
		info->usable=qfalse;
		return -1;
	}

	info->extra_count = 0;

	if(DB_Write_UserExtrasDBheader(info->extras_file)) {
		info->usable=qfalse;
		return -1;
	}
	return 0;
}

static int DB_CreateUserDBMainFile(void)
{
	db_users_info_t	*info=&db_users_info;

	// some information that is needed
	info->records_count = 0;
	G_DB_File_Open(&info->db_file, DB_USERS_FILENAME, DB_FILEMODE_TRUNCATE);

	if( !info->db_file ) {
		G_LogPrintf("  Failed to create database file.\n");
		return -1;
	}

	if( !DB_Write_UserDBheader(info->db_file) ) {
		info->usable=qtrue;
	}
	return 0;
}

static int DB_CreateUserDB(void)
{
	db_users_info_t	*info=&db_users_info;

	// some information that is needed
	info->records_count=0;
	info->extra_count=0;

	G_DB_File_Open(&info->db_file, DB_USERS_FILENAME, DB_FILEMODE_TRUNCATE);

	if(!info->db_file) {
		G_LogPrintf("  Failed to create database file.\n");
		return -1;
	}

	G_DB_File_Open(&info->extras_file, DB_USERSEXTRA_FILENAME, DB_FILEMODE_TRUNCATE);

	if(!info->extras_file) {
		G_LogPrintf("  Failed to create database file.\n");
		return -1;
	}

	if(!DB_Write_UserDBheader(info->db_file) && !DB_Write_UserExtrasDBheader(info->extras_file)) {
		info->usable=qtrue;
	}
	return 0;
}

int DB_ConvertExtrasFrom_03(void)
{
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;
	g_shrubbot_userextra_f03_t user_extra_read;
	g_shrubbot_userextra_f_t user_extra_write;
	FILE *old_db;
	int i, users, bytes;

	// close file if open
	G_DB_File_Close(&info->extras_file);

	G_LogPrintf("  User database file identified to be an old version. (Used up to silEnT version 0.5.2)\n");
	G_LogPrintf("  Converting the file to the current version.\n");

	// rename old file and keep it as backup
	G_DB_RenameFile(DB_USERSEXTRA_FILENAME, "userxdb_v03.db");

	// create new extras
	if( DB_CreateExtrasDB() == -1 ) {
		return -1;
	}
	if( G_DB_File_Open(&old_db, "userxdb_v03.db", DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Failed to open the old file version.\n");
		return -1;
	}

	G_DB_ReadBlockFromDBFile(old_db, (void*)&header, sizeof(header), 0);
	G_DB_SetFilePosition(info->extras_file, sizeof(db_users_fileheader_t));

	users = header.records_count;
	for(i=0 ; i < users ; i++) {
		bytes = G_DB_ReadBlockFromDBFile(old_db, (void*)&user_extra_read, sizeof(user_extra_read), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in reading from the user database file.\n");
			// error situation, do something here
			break;
		}

		// fast binary copy, the structs are the same for the most parts
		memset(&user_extra_write, 0 , sizeof(user_extra_write));
		memcpy(&user_extra_write, &user_extra_read, sizeof(user_extra_read));
		// rest is all new and memset to 0

		bytes = G_DB_WriteBlockToFile(info->extras_file, (void*)&user_extra_write, sizeof(user_extra_write), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in writing to the new database file.\n");
			// error situation, do something here
			break;
		}

		info->extra_count++;
	}
	G_DB_File_Close(&old_db);
	DB_Write_UserExtrasDBheader(info->extras_file);
	G_DB_File_Close(&info->extras_file);

	G_LogPrintf("  %d records converted from the old file.\n", db_users_info.extra_count);

	// The init will continue normally from here so reopening the new db for good format
	if( G_DB_File_Open(&info->extras_file, DB_USERSEXTRA_FILENAME, DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Unexpected database conversion error.\n");
		G_LogPrintf("  Save all database files and consult silEnT developers for more info.\n");
		return -1;
	}

	return 0;
}

int DB_ConvertExtrasFrom_02(void)
{
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;
	g_shrubbot_userextra_f02_t user_extra_read;
	g_shrubbot_userextra_f_t user_extra_write;
	FILE *old_db;
	int i, users, bytes;

	// close file if open
	G_DB_File_Close(&info->extras_file);

	G_LogPrintf("  User database file identified to be an old version. (Used up to silEnT version 0.4.0)\n");
	G_LogPrintf("  Converting the file to the current version.\n");

	// rename old file and keep it as backup
	G_DB_RenameFile(DB_USERSEXTRA_FILENAME, "userxdb_v02.db");

	// create new extras
	if(DB_CreateExtrasDB() == -1) {
		return -1;
	}
	if( G_DB_File_Open(&old_db, "userxdb_v02.db", DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Failed to open the old file version.\n");
		return -1;
	}

	G_DB_ReadBlockFromDBFile(old_db, (void*)&header, sizeof(header), 0);
	G_DB_SetFilePosition(info->extras_file, sizeof(db_users_fileheader_t));

	users = header.records_count;
	for(i=0 ; i < users ; i++) {
		bytes = G_DB_ReadBlockFromDBFile(old_db, (void*)&user_extra_read, sizeof(user_extra_read), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in reading from the user database file.\n");
			// error situation, do something here
			break;
		}

		// fast binary copy, the structs are the same for the most parts
		memset(&user_extra_write, 0 , sizeof(user_extra_write));
		memcpy(&user_extra_write, &user_extra_read, SIL_SHRUBBOT_DB_GUIDLEN);
		// hop over the silent guid and copy greeting + greeting sound
		memcpy(&user_extra_write.greeting, &user_extra_read.greeting, 384); // greeting + greetingsound
		// rest is all new and memset to 0

		bytes = G_DB_WriteBlockToFile(info->extras_file, (void*)&user_extra_write, sizeof(user_extra_write), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in writing to the new database file.\n");
			// error situation, do something here
			break;
		}

		info->extra_count++;
	}
	G_DB_File_Close(&old_db);
	DB_Write_UserExtrasDBheader(info->extras_file);
	G_DB_File_Close(&info->extras_file);

	G_LogPrintf("  %d records converted from the old file.\n", db_users_info.extra_count);

	// The init will continue normally from here so reopening the new db for good format
	if( G_DB_File_Open(&info->extras_file, DB_USERSEXTRA_FILENAME, DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Unexpected database conversion error.\n");
		G_LogPrintf("  Save all database files and consult silEnT developers for more info.\n");
		return -1;
	}

	return 0;
}

int DB_ConvertFrom_03(void)
{
	g_shrubbot_user_f03_t	user_read;
	g_shrubbot_user_f_t		user_write;
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;
	FILE *old_db;
	int i, users, bytes;
	int firstpartsize = sizeof(user_read)-sizeof(user_read.ident)-sizeof(user_read.ident_flags)-sizeof(user_read.padding)-sizeof(user_read.last_xp_save);

	// close file if open
	G_DB_File_Close(&info->db_file);

	G_LogPrintf("  User database file identified to be an old version. (Used up to silEnT version 0.5.2)\n");
	G_LogPrintf("  Converting file to the current version.\n");

	// rename old file and keep it as backup
	G_DB_RenameFile(DB_USERS_FILENAME, "userdb_v03.db");

	// create new database
	if( DB_CreateUserDBMainFile() == -1 ) {
		return -1;
	}
	if( G_DB_File_Open(&old_db, "userdb_v03.db", DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Failed to open the old version of the file.\n");
		return -1;
	}

	// copy one by one to a new file
	G_DB_ReadBlockFromDBFile(old_db, &header, sizeof(db_users_fileheader_t), 0);
	G_DB_SetFilePosition(info->db_file, sizeof(db_users_fileheader_t));

	users = header.records_count;
	for(i=0 ; i < users ; i++) {
		bytes = G_DB_ReadBlockFromDBFile( old_db, (void*)&user_read, sizeof(user_read), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in reading the user database file.\n");
			// error situation, do something here
			break;
		}

		// fast binary copy, the structs are the same for the most parts
		memset(&user_write, 0 , sizeof(user_write));
		memcpy(&user_write, &user_read, firstpartsize);
		// now fixing the fields starting from the ident
		memcpy(&user_write.ident, &user_read.ident, sizeof(user_read.ident));
		user_write.ident_flags = user_read.ident_flags;
		user_write.ident_flags |= SIL_DBIDENTFLAG_SHORT;
		user_write.last_xp_save = user_read.last_xp_save;

		// mutes must be cleared when converting from below 0.4
		user_write.mutetime = 0;

		bytes = G_DB_WriteBlockToFile(info->db_file, &user_write, sizeof(user_write), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in writing to the new user database file.\n");
			// error situation, do something here
			break;
		}

		info->records_count++;
	}
	G_DB_File_Close(&old_db);
	DB_Write_UserDBheader(info->db_file);
	G_DB_File_Close(&info->db_file);

	G_LogPrintf("  %d records converted from the old file.\n", db_users_info.records_count);

	// The init will continue normally from here so reopening the new db for good format
	if( G_DB_File_Open(&info->db_file, DB_USERS_FILENAME, DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Unexpected database conversion error.\n");
		G_LogPrintf("  Save all database files and consult silEnT developers for more info.\n");
		return -1;
	}

	return 0;
}

// return -1 on failures and 0 for success
int DB_ConvertFrom_02(void)
{
	g_shrubbot_user_f02_t	user_read;
	g_shrubbot_user_f_t		user_write;
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;
	FILE *old_db;
	int i, users, bytes;
	int firstpartsize = sizeof(user_read.guidHash) + sizeof(user_read.guid);
	int endpartsize = sizeof(user_read.ident) + sizeof(user_read.ident_flags) + sizeof(user_read.padding);
	int secondpartsize = sizeof(user_read) - (sizeof(user_read.guidHash) + sizeof(user_read.guid) + endpartsize );

	// close file if open
	G_DB_File_Close(&info->db_file);

	G_LogPrintf("  User database file identified to be an old version. (Used up to silEnT version 0.4.0)\n");
	G_LogPrintf("  Converting file to the current version.\n");

	// rename old file and keep it as backup
	G_DB_RenameFile(DB_USERS_FILENAME, "userdb_v02.db");

	// create new database
	if( DB_CreateUserDBMainFile() == -1 ) {
		return -1;
	}
	if( G_DB_File_Open(&old_db, "userdb_v02.db", DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Failed to open the old version of the file.\n");
		return -1;
	}

	// copy one by one to a new file
	G_DB_ReadBlockFromDBFile(old_db, &header, sizeof(db_users_fileheader_t), 0);
	G_DB_SetFilePosition(info->db_file, sizeof(db_users_fileheader_t));

	users = header.records_count;
	for(i=0 ; i < users ; i++) {
		bytes = G_DB_ReadBlockFromDBFile(old_db, (void*)&user_read, sizeof(user_read), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in reading the user database file.\n");
			// error situation, do something here
			break;
		}

		// fast binary copy, the structs are the same for the most parts
		memset(&user_write, 0 , sizeof(user_write));
		memcpy((void*)&user_write.pbgHash, &user_read, firstpartsize);
		memcpy((void*)&user_write.name, &user_read.name, secondpartsize);
		// the last part is little messier with changing fields that they must be done by hand
		memcpy(&user_write.ident, &user_read.ident, sizeof(user_read.ident));
		user_write.ident_flags = user_read.ident_flags;
		user_write.ident_flags |= SIL_DBIDENTFLAG_SHORT;

		// mutes must be cleared when converting from below 0.4
		user_write.mutetime = 0;

		G_DB_WriteBlockToFile(info->db_file, (void*)&user_write, sizeof(user_write), -1);
		if(bytes != 1) {
			G_LogPrintf("  Error condition in writing to the new user database file.\n");
			// error situation, do something here
			break;
		}

		info->records_count++;
	}
	G_DB_File_Close(&old_db);
	DB_Write_UserDBheader(info->db_file);
	G_DB_File_Close(&info->db_file);

	G_LogPrintf("  %d records converted from the old file.\n", db_users_info.records_count);

	// The init will continue normally from here so reopening the new db for good format
	if( G_DB_File_Open(&info->db_file, DB_USERS_FILENAME, DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Unexpected database conversion error.\n");
		G_LogPrintf("  Save all database files and consult silEnT developers for more info.\n");
		return -1;
	}

	return 0;
}

// return -1 on failures and 0 for success
int DB_ConvertFrom_01(void)
{
	g_shrubbot_user_f01_t	user_read;
	g_shrubbot_user_f_t		user_write;
	g_shrubbot_userextra_f_t user_extra_write;
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;
	FILE *old_db;
	int i, users, bytes;

	// close file if open
	G_DB_File_Close(&info->db_file);

	G_LogPrintf("  Database file identified to be an old version. (Used up to silEnT version 0.2.1)\n");
	G_LogPrintf("  Converting database to the current version.\n");

	// rename old firle for reading and keep it as backup
	G_DB_RenameFile(DB_USERS_FILENAME, "userdb_v01.db");

	// create new database
	if( DB_CreateUserDB() == -1 ) {
		return -1;
	}
	if( G_DB_File_Open(&old_db, "userdb_v01.db", DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Failed to open old version of the file\n");
		return -1;
	}

	// copy one by one to a new file
	G_DB_ReadBlockFromDBFile(old_db, &header, sizeof(db_users_fileheader_t), 0);
	G_DB_SetFilePosition(info->db_file, sizeof(db_users_fileheader_t));

	users = header.records_count;
	for(i=0 ; i < users ; i++) {
		bytes = G_DB_ReadBlockFromDBFile(old_db, (void*)&user_read, sizeof(user_read), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in reading the user database file.\n");
			// error situation, do something here
			break;
		}

		// fast binary copy, the structs are the same for the most parts
		memset(&user_write, 0 , sizeof(user_write));
		memcpy(&user_write.pbgHash, &user_read, 36);
		memcpy(&user_write.name, &user_read.name, 156);
		memcpy(&user_write.time, &user_read.time, 60);

		// mutes must be cleared when converting from below 0.4
		user_write.mutetime = 0;

		bytes = G_DB_WriteBlockToFile(info->db_file, (void*)&user_write, sizeof(user_write), -1);
		if( bytes < 0 ) {
			G_LogPrintf("  Error condition in writing to the new user database file.\n");
			// error situation, do something here
			break;
		}

		info->records_count++;

		if(user_read.greeting[0] || user_read.greeting_sound[0]) {
			memset(&user_extra_write, 0, sizeof(user_extra_write));
			memcpy(&user_extra_write.pb_guid, &user_read.guid, SIL_SHRUBBOT_DB_GUIDLEN);
			memcpy(&user_extra_write.greeting, &user_read.greeting, 384);

			G_DB_WriteBlockToFile(info->extras_file, (void*)&user_extra_write, sizeof(user_extra_write), -1);
			if(bytes != 1) {
				G_LogPrintf("  Error condition in writing to the new user database file.\n");
				// error situation, do something here
				break;
			}

			info->extra_count++;
		}
	}
	G_DB_File_Close(&old_db);
	DB_Write_UserDBheader(info->db_file);
	G_DB_File_Close(&info->db_file);
	DB_Write_UserExtrasDBheader(info->extras_file);
	G_DB_File_Close(&info->extras_file);
	G_LogPrintf("  %d records converted from the old file.\n", db_users_info.records_count);

	// The init will continue normally from here so reopening the new db for good format
	if( G_DB_File_Open(&info->db_file, DB_USERS_FILENAME, DB_FILEMODE_READ) == NULL ) {
		G_LogPrintf("  Unexpected database conversion error.\n");
		G_LogPrintf("  Save all database files and consult silEnT developers for more info.\n");
		return -1;
	}
	// extras have their own open
	return 0;
}

static int32_t DB_OpenExtrasDBFile( void )
{
	int					    bytes;
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;

	G_DB_File_Open(&info->extras_file, DB_USERSEXTRA_FILENAME, DB_FILEMODE_READ);
	if(!info->extras_file) {
		G_LogPrintf("  User database file does not exist.\n");
		if(DB_CreateExtrasDB() == -1) {
			G_LogPrintf("  Failed creating user database file userxdb.db.\n");
			return -1;
		}
		G_LogPrintf("  New user database file userxdb.db created.\n");
		info->extra_count = 0;
		return 1;
	} else {
		memset(&header, 0, sizeof(header));
		bytes = G_DB_ReadBlockFromDBFile(info->extras_file, &header, sizeof(header), 0);
		if( bytes < 0 || memcmp(header.db_version, DB_USERSEXTRA_VERSION, DB_USERS_VERSIONSIZE)) {
			if( !memcmp(header.db_version, DB_USERSEXTRA_VERSION_02, DB_USERS_VERSIONSIZE) ) {
				if( DB_ConvertExtrasFrom_02() == -1 ) {
					G_LogPrintf("  Failed converting old userxdb.db file to the version used with this silEnT.\n");
					return -2;
				} else {
					G_LogPrintf("  Old userxdb.db converted to the version used from silEnT 0.5.0 onwards.\n");
				}
			} else if( !memcmp(header.db_version, DB_USERSEXTRA_VERSION_03, DB_USERS_VERSIONSIZE) ) {
				if( DB_ConvertExtrasFrom_03() == -1 ) {
					G_LogPrintf("  Failed converting old userxdb.db file to the version used with this silEnT.\n");
					return -2;
				} else {
					G_LogPrintf("  Old userxdb.db converted to the version used from silEnT 0.6.0 onwards.\n");
				}
			} else {
				G_LogPrintf("  Existing userxdb.db file is for wrong server version or corrupted.\n");
				return -2;
			}
		} else {
			info->extra_count = header.records_count;
		}
	}

	return 0;
}

/**
 * Function opens the main database file, userdb.db.
 *
 * @return	0 - file was opened,
 *			1 - created new file,
 *			-1 failed creating new file,
 *			-2 failed converting old file
 */
static int32_t DB_OpenMainDBFile( void )
{
	int 					bytes;
	db_users_fileheader_t	header;
	db_users_info_t			*info=&db_users_info;

	G_DB_File_Open(&info->db_file, DB_USERS_FILENAME, DB_FILEMODE_READ);

	if(!info->db_file) {
		G_LogPrintf("  User database file does not exist.\n");
		if(DB_CreateUserDBMainFile() == -1) {
			G_LogPrintf("  Failed creating user database file userdb.db.\n");
			return -1;
		}
		G_LogPrintf("  New user database file userdb.db created.\n");
		return 1;
	} else {
		// If file is the wrong type, fail and do not create db
		memset(&header, 0, sizeof(header));
		bytes = G_DB_ReadBlockFromDBFile(info->db_file, &header, sizeof(header), 0);

		if( (bytes < 0) || memcmp(header.db_version, DB_USERS_VERSION, DB_USERS_VERSIONSIZE)) {
			DB_Files_Close();
			if( !memcmp(header.db_version, DB_USERS_VERSION_01, DB_USERS_VERSIONSIZE) ) {
				// old version of database (used up to silEnT 0.2.1 public versions)
				if(DB_ConvertFrom_01() == -1) {
					G_LogPrintf("  Failed converting old userdb.db to the version used in this silEnT version.\n");
					return -2;
				} else {
					G_LogPrintf("  Old userdb.db converted to the version used from silEnT 0.5.0 onwards.\n");
				}
			} else if( !memcmp(header.db_version, DB_USERS_VERSION_02, DB_USERS_VERSIONSIZE) ) {
				if(DB_ConvertFrom_02() == -1) {
					G_LogPrintf("  Failed converting old userdb.db to the version used in this silEnT version.\n");
					return -2;
				} else {
					G_LogPrintf("  Old userdb.db converted to the version used from silEnT 0.5.0 onwards.\n");
				}
			} else if( !memcmp(header.db_version, DB_USERS_VERSION_03, DB_USERS_VERSIONSIZE) ) {
				// old version of database (used up to silEnT 0.5.2 public versions)
				if( DB_ConvertFrom_03() == -1 ) {
					G_LogPrintf("  Failed converting old userdb.db to the version used in this silEnT version.\n");
					return -2;
				} else {
					G_LogPrintf("  Old userdb.db converted to the version used from silEnT 0.6.0 onwards.\n");
				}
			} else {
				// does not match even old databse versions
				G_LogPrintf("  Existing database file is for wrong server version or corrupted.\n");
				return -2;
			}
		} else {
			// Now just filling the info for later use
			info->records_count = header.records_count;
		}
	}
	// file was just opened
	return 0;
}

static void DB_CountAndStoreFetchAverage( qboolean reset )
{
	if( silent_miscflags.integer & SIL_MISCFLAGS_LOGDBFETCH ) {
		G_LogPrintf("  Map average fetch: %f from %d searches\n", db_users_info.fetch_average, db_users_info.fetch_count);
	}
}

void G_DB_UpdateAliases( void )
{
	g_shrubbot_buffered_users_t *users=user_buffer;

	// go through all buffered players and update data as needed

	// update aliases data
	while( users ) {
		if( users->currentAlias.clean_name[0] ) {
			users->currentAlias.last_seen = level.realtime;
			users->currentAlias.time_played = level.realtime - users->currentAlias.first_seen;
			G_DB_UpdateAlias(users->user->user.sil_guid, &users->currentAlias, users->user->user.guidHash);
			// reset for next time
			users->currentAlias.first_seen = level.realtime;
			users->currentAlias.time_played = 0;
		}
		users=users->next;
	}
}

// silencer - hackish workaround for the problems with loading the dlls
// and database initialization with debug build on linux
// it's hard to track down, so I came up with this workaround
// this is only enabled for DEBUG builds
// !! Do remember to be extremely cautious when using DEBUG builds for testing.
// It will not trigger all the errors that will happen with release build. !!
#ifdef _DEBUG
#  ifdef __linux__
#    warning DEBUG build will not trigger all errors. Use release for blackbox testing.
#    define DLOPEN_HACK
     static int dlopen_hack = 0;
#  endif
#endif

////////////////////////////////////////////////////////////////////////
// DB_InitDatabase
//
// Function initialises the user databaseb system. If there aren't
// existing database with the given name, the function creates one.
// If there is an existing file for the given name, the function attempts
// to check it is a valid database file and if it's not, it will not
// overwrite it.
//
// return value: -1 if failed,
//               0 if any success
int32_t	G_DB_InitDatabase(qboolean force)
{
	db_users_info_t			*info=&db_users_info;
	static int				runtimes=0;

	G_LogPrintf("*=====INITIALISING USER DATABASE\n");

#ifdef DLOPEN_HACK
	if(runtimes && !force && dlopen_hack > 0)
#else
	if(runtimes && !force)
#endif
	{
		G_LogPrintf("  Database is already initialised.\n");
		if(info->usable==qfalse) {
			G_LogPrintf("*=====DATABASE IS NOT IN USE\n");
		} else {
			G_LogPrintf("*=====DATABASE READY FOR USE\n");
		}
		return 0; // function gets run only once unless forced
	}

	runtimes++;
	memset((void*)&db_users_info, 0, sizeof(*info));
	// from this point onward, all failures mean the database is not usable
	info->usable = qfalse;
	// clearing search results
	memset(&search_cache,0,sizeof(search_cache));

	if(!g_dbDirectory.string[0]) {
		G_LogPrintf("  Database directory is not set.\n");
		G_LogPrintf("*=====DATABASE IS NOT IN USE\n");
		return -1;
	}

	// making sure its safe to initialise db
	DB_DestroyBuffers();
	DB_DestroyCaches();
	DB_UserDB_Close();

	if( G_DB_InitDirectoryPath() == -1 ) {
		G_LogPrintf("  Directory paths exceed the buffer length. Bailing out.\n");
		G_LogPrintf("*=====DATABASE IS NOT IN USE\n");
		return -1;
	}

	// no truncating for freshly opened db
	info->truncate = qfalse;

	G_LogPrintf("  * Opening user database file userdb.db.\n");
	if( DB_OpenMainDBFile() < 0 ) {
		G_LogPrintf("*=====DATABASE IS NOT IN USE\n");
		return -1;
	}
	G_LogPrintf("  * Opening user database file userxdb.db.\n");
	if( DB_OpenExtrasDBFile() < 0 ) {
		G_LogPrintf("*=====DATABASE IS NOT IN USE\n");
		// closing already opened userdb.db file and leaving
		G_DB_File_Close(&info->db_file);
		return -1;
	}

	info->usable = qtrue;

	if( info->records_count || info->extra_count ) {
		G_LogPrintf("  * User database files open. Caching database.\n");
		// files good, reading the data
#ifdef DEBUG_USERSDB
		DB_DebugReadUsersFromDB();
#else
		DB_ReadUsersFromDB();
#endif
		DB_ReadExtrasFromDB();
		// all done
	}

	// aliases database
	G_DB_InitAliases();

	DB_Files_Close();

	G_LogPrintf("*=====DATABASE READY FOR USE\n");
	return 0;
}

void G_DB_IssueFileOptimize(void)
{
	db_users_info.optimize = qtrue;
	G_LogPrintf("User database filesystem optimization issued to the next closing of the database.\n");
}

void G_DB_IssueCleanup(void)
{
	db_users_info.cleanup = qtrue;
	G_LogPrintf("User database clean up issued to the next intermission.\n");
}

void G_DB_IntermissionActions(void)
{
	// lengthy actions
	G_DB_PruneUsers();

	if( db_users_info.cleanup ) {
		DB_DatabaseCleanUp();
	}
}

void G_DB_CloseDatabase(void)
{
	G_LogPrintf("*=====CLOSING DATABASE\n");
	if(db_users_info.usable) {
		G_DB_UpdateAliases();
		// cleanup extras file before writing the cached db to file
		// if records are removed at this point (i.e. pruning, the extras are already handled correctly)
		DB_ExtrasCleanup();

		if(db_users_info.optimize) {
			G_LogPrintf("  Database filesystem optimization issued.\n");
			DB_WriteUsersOptimizeFile();
			db_users_info.fetch_average = 0;
			G_LogPrintf("  Database filesystem optimization done.\n");
		} else if(db_users_info.truncate) {
			G_LogPrintf("  Database filesystem cleanup needed.\n");
			DB_WriteUsersTruncFile();
			G_LogPrintf("  Database filesystem cleanup done.\n");
		} else {
			DB_WriteUsersToDB(qfalse);
		}
		// aliases database
		//G_DB_AliasesUpdate();
		G_DB_CloseAliases();
		// update fetch average
		DB_CountAndStoreFetchAverage(db_users_info.optimize);
		// extra data gets truncated always
		DB_WriteExtrasToDB(qfalse);
	}

	DB_DestroyBuffers();
	if(db_users_info.usable==qfalse) {
		G_LogPrintf("  Database is not in use. Cleaning only buffers.\n");
	}
	G_LogPrintf("  Buffers cleaned.\n");
	if(db_users_info.usable==qtrue) {
		DB_DestroyCaches();
		G_LogPrintf("  Big Memory Cache cleaned.\n");
	}
	DB_UserDB_Close();
	G_LogPrintf("*=====DATABASE IS CLOSED\n");
#ifdef DLOPEN_HACK
	dlopen_hack--;
#endif
}

/*
	Note that we purposely won't update the userData in anywhere else but here.
	This is a security measure.
	We assume all legitimate admins have their PunkBuster enabled.
	This does not interfere with XP save but it prevents anyone taking over admin commands belonging
	to some other user in case they guid spoof in the middle of a level.
*/
/*
	A word about combining the 2 guid searches.

	Doing 2 passes to find the record.
	Average case when found using silent guid: N/2, worst case N
	Average case when found using pb guid: N+(N/2), worst case 2N
	Doing single pass comparing both
	Average case when found using silent guid: N, worst case 2N
	Average case when found using pb guid: N worst case 2N

	Overall estimation 2 passes:
	By far most times players are already known to server, this makes the overall average less then N and worst case 2N
	Overall estimation 1 pass:
	Average N, worst case 2N

	When counting the time used to fetch the memory, this might be in favour of single pass, but it is impossible to estimate currently.
*/

/**
* Function is called as soon as player is ready to be attached to the database. The player must have supplied
* silEnT GUID and this is checked before the call to this function. This is the earliest point when a player data
* attached to the player can be handled.
*
* @param ent The player entity that is getting database data attached to him.
* @param pb_guid The PunkBuster GUID of the player if he has supplied it in the userinfo.
*/
void G_DB_ClientConnect(gentity_t *ent, char* pb_guid)
{
	g_shrubbot_buffered_users_t* user=NULL;
	uint32_t guidHash = 0;
	uint32_t sil_guidHash;
	uint32_t i;
	char *pointer;
	char guid[32];
	char sil_guid[32];
	float faverage;
	time_t t;

	// ensure the pointer will not point to invalid data
	g_clientSInfos[ent-g_entities].userData = NULL;
	g_clientSInfos[ent-g_entities].extraData = NULL;
	g_clientSInfos[ent-g_entities].alias = NULL;

	// we first attempt to find using the silent guid
	for( i = 0; i < 32 && ent->client->sess.guid[i] ; i++) {
		sil_guid[i] = toupper(ent->client->sess.guid[i]);
	}
	if( i != 32 ) {
		// no silent GUID
		return;
	}

	sil_guidHash = BG_hashword((const uint32_t*)sil_guid, 8, 0);

	db_users_info.lastfetchN = 0;

	user = DB_GetUserNode(sil_guidHash, sil_guid);

	// try to find using PB GUIDS
	if( !user && pb_guid[0] ) {
		for( i = 0; i < 32 && pb_guid[i] ; i++) {
			guid[i] = toupper(pb_guid[i]);
		}

		if( i == 32 ) {
			guidHash = BG_hashword((const uint32_t*)guid, 8, 0);
			user = DB_GetUserNodePB(guidHash, guid);
		}
		// update values for the future use
		if( user ) {
			g_shrubbot_userextra_f_t *userExt;
			if( user->user->user.guidHash ) {
				G_CheatLogPrintf("silEnT GUID: Player %s PB GUID (%.32s) changed silEnT GUID from (%.32s) to (%.32s)\n", user->user->user.name, pb_guid, user->user->user.sil_guid, sil_guid);
			}
#ifdef DYNAMIC_MODULES
			StatsMod_GUIDChange(user->user->user.sil_guid, sil_guid);
#endif
			user->user->user.guidHash = sil_guidHash;
			memcpy(user->user->user.sil_guid, sil_guid, SIL_SHRUBBOT_DB_GUIDLEN);
			// check if user extra needs update for silent guid
			userExt = DB_FindUserExtras( &user->user->user ); // <-- the naming sucks here
			if( userExt ) {
				memcpy(userExt->sil_guid, sil_guid, SIL_SHRUBBOT_DB_GUIDLEN);
			}
		}
	}
	// calculate average fetch N
	if( db_users_info.records_count ) {
		faverage = db_users_info.fetch_average * db_users_info.fetch_count + ((float)db_users_info.lastfetchN / db_users_info.records_count);
		db_users_info.fetch_count++;
		db_users_info.fetch_average = faverage / db_users_info.fetch_count;
	}
	// if still no user record, create one
	if( !user ) {
		// this is totally new user
		user = DB_NewUserNode(sil_guidHash, sil_guid);
		// copy the PB GUID if any, guidHash was created when searching the db using pb guid
		if( user && guidHash ) {
			memcpy(user->user->user.pb_guid, guid, sizeof(user->user->user.pb_guid));
			user->user->user.pbgHash = guidHash;
		}
	}

	// give the game good direct pointers for data
	if( user ) {
		g_clientSInfos[ent-g_entities].userData = &user->user->user;
		g_clientSInfos[ent-g_entities].extraData = DB_FindUserExtras(&user->user->user);
		g_clientSInfos[ent-g_entities].alias = &user->currentAlias;
		user->user->user.ident_flags |= SIL_DBGUID_VALID;
	} else {
		G_LogPrintf("G_DB_ClientConnect: Client buffering error, system memory is likely exhausting!");
		return;
	}

	// copy the IP if any, cut the result from port
	if( ent->client->sess.ip[0] ) {
		strncpy(user->user->user.ip,ent->client->sess.ip,SIL_SHRUBBOT_IPLEN);
		user->user->user.ip[SIL_SHRUBBOT_IPLEN-1] = '\0';
		pointer = strstr(user->user->user.ip,":");
		if(pointer) {
			*pointer = '\0';
		}
	}

	// set up for alias tracking
	memset(&user->currentAlias, 0, sizeof(user->currentAlias));

	// check if the player is muted and if the name must be forced from the database, this will prevent forcing names from
	// the etmain profile (or whatever profile player has before connecting)
	if( g_muteRename.integer && user->user->user.mutetime && user->user->user.name[0] ) {
		char	userinfo[MAX_INFO_STRING];

		Q_strncpyz(ent->client->pers.netname, user->user->user.name, sizeof(ent->client->pers.netname));

		// this is needed so that the server recognizes the name changes after the player gets unmuted
		trap_GetUserinfo(ent-g_entities, userinfo, sizeof(userinfo));
		Info_SetValueForKey( userinfo, "name", user->user->user.name);
		trap_SetUserinfo(ent-g_entities, userinfo);
	}

	// copy the name if any
	if( ent->client->pers.netname[0] ) {
		g_shrubbot_user_f_t *data = &user->user->user;
		memcpy(data->name, ent->client->pers.netname, sizeof(data->name));
		// since the name string always includes the NUL, we ensure it also in here
		data->name[MAX_NAME_LENGTH-1] = '\0';
		// the sanitized name
		Q_strncpyz(data->sanitized_name, G_DB_SanitizeName(ent->client->pers.netname), MAX_NAME_LENGTH);
		// the alias data
		user->currentAlias.first_seen = level.realtime;
		user->currentAlias.last_seen = level.realtime;
		user->currentAlias.time_played = 0;
		memcpy(user->currentAlias.name, data->name, sizeof(user->currentAlias.name));
		Q_strncpyz(user->currentAlias.clean_name, data->sanitized_name, sizeof(user->currentAlias.clean_name));
	}

	user->flags |= SIL_DBUSERFLAG_CONNECTED; // connect user
	user->flags |= SIL_DBUSERFLAG_FULLINIT; // note that the player stats can be updated

	// admin protection
	// if in warmup, do not attempt authentication,
	// it will cause problem when server restarts for the actual game
	if( (ent->client->sess.misc_flags & SESSION_MISCALLENOUS_AUTHOK)
		|| g_protectMinLevel.integer > user->user->user.level
		|| g_protectMinLevel.integer == -1 ) {
		// previously authenticated or the level is not protected
		Sil_AllowClientAdmin( ent );
	} else if( g_gamestate.integer != GS_WARMUP ) {
		// only operate keys when not in warmup
		if( G_DB_UserHasKeys(ent) == qtrue ) {
			// request confirmation from the client
			Sil_GenerateClientCheck( ent );
		} else {
			Sil_SetClientAwaitingConfirmation( ent );
		}
	}

	// set level long values stored
	ent->client->pers.panzerSelfKills = user->panzerSelfKills;

	if( !time(&t) ) {
		return;
	}
	// set the time, we don't want to lose users just yet (invoke truncates for users that have not played)
	user->user->user.time = t;
}

/**
* Function is called to store the player name to the database records and handle alias changes.
* This is called when the player names change. This is not called for the initial player name.
*
* @param ent The client entity which has a new name.
*/
void G_DB_SetClientName(gentity_t *ent)
{
	g_shrubbot_buffered_users_t* user = NULL;
	g_shrubbot_user_f_t *data;
	db_alias_t	*alias;
	uint32_t guidHash;
	uint32_t i;
	char guid[32];

	if( g_clientSInfos[ent-g_entities].userData ) {
		data = (g_shrubbot_user_f_t*)g_clientSInfos[ent-g_entities].userData;
		alias = (db_alias_t*)g_clientSInfos[ent-g_entities].alias;
	} else {
		for( i=0; i < 32 && ent->client->sess.guid[i] ; i++) {
			guid[i] = toupper(ent->client->sess.guid[i]);
		}

		if( i != 32 ) {
			return;
		}

		guidHash = BG_hashword((const uint32_t*)guid, 8, 0);

		user = DB_GetUserNode(guidHash, guid);

		if( !user ) {
			return;
		}

		data = &user->user->user;
		alias = &user->currentAlias;
	}

	if( ent->client->pers.netname[0] ) {
		if( alias && alias->clean_name[0] ) {
			alias->last_seen = level.realtime;
			alias->time_played = alias->last_seen - alias->first_seen;
			G_DB_UpdateAlias(data->sil_guid, alias, data->guidHash);
		}

		// copy the name if any, we don't have names for clients before the userinfo has changed at least once
		memcpy(data->name, ent->client->pers.netname, sizeof(data->name));

		// since the name string always include the NUL, we ensure it also in here
		data->name[MAX_NAME_LENGTH-1] = '\0';

		// the sanitized name
		Q_strncpyz(data->sanitized_name, G_DB_SanitizeName(ent->client->pers.netname), MAX_NAME_LENGTH);

		// alias handling
		alias->first_seen = level.realtime;
		alias->time_played = 0;
		memcpy(alias->name, data->name, sizeof(alias->name));
		Q_strncpyz(alias->clean_name, data->sanitized_name, sizeof(alias->clean_name));
	}
}

/**
*  Function is used to restore name to the player from the database.
*
*  @param ent The player entity to handle.
*  @return true if name was restored, false otherwises
*/
qboolean G_DB_RestoreClientName(gentity_t *ent)
{
	char userinfo[MAX_INFO_STRING];
	g_shrubbot_user_f_t *data = NULL;
	int clientNum = ent-g_entities;

	if( (g_muteRename.integer == 0) || (ent->client->sess.auto_unmute_time == 0) ) {
		return qfalse;
	}

	if( g_clientSInfos[clientNum].userData ) {
		data = (g_shrubbot_user_f_t*)g_clientSInfos[clientNum].userData;
	}

	if( !data || (data->name[0] == '\0') ) {
		return qfalse;
	}

	Q_strncpyz(ent->client->pers.netname, data->name, sizeof(ent->client->pers.netname));

	// restore name in userinfo too
	trap_GetUserinfo(clientNum, userinfo, sizeof(userinfo));
	Info_SetValueForKey( userinfo, "name", data->name);
	trap_SetUserinfo(clientNum, userinfo);

	CPx(clientNum, va("print \"Your name %s is restored from the database record because of your mute\n\"", data->name));

	return qtrue;
}

void G_DB_StoreCurrentAlias(gentity_t *ent)
{
	db_alias_t	*alias;

	if( !g_clientSInfos[ent-g_entities].alias ) {
		return;
	}

	alias = (db_alias_t*)g_clientSInfos[ent-g_entities].alias;

	if( alias->clean_name[0] ) {
		alias->last_seen = level.realtime;
		alias->time_played = alias->last_seen - alias->first_seen;
		G_DB_UpdateAlias( ent->client->sess.guid, alias, 0);
		// reset values so next update wont distort values
		alias->first_seen = level.realtime;
		alias->time_played = 0;
	}
}

void G_DB_SetClientGUIDValid(g_shrubbot_user_handle_t *handle)
{
	handle->user->ident_flags |= SIL_DBGUID_VALID;
}

qboolean G_DB_IsClientGUIDValid(const g_shrubbot_user_handle_t *handle)
{
	if( handle->user->ident_flags & SIL_DBGUID_VALID ) {
		return qtrue;
	}
	return qfalse;
}

char *G_DB_GetClientGUIDStub( gentity_t *ent )
{
	g_shrubbot_buffered_users_t* user=NULL;
	g_shrubbot_user_f_t *data;
	uint32_t guidHash;
	uint32_t i;
	char guid[32];

	if(g_clientSInfos[ent-g_entities].userData) {
		data = (g_shrubbot_user_f_t*)g_clientSInfos[ent-g_entities].userData;
	} else {
		for(i=0; i < 32 && ent->client->sess.guid[i] ;i++) {
			guid[i]=toupper(ent->client->sess.guid[i]);
		}
		if(i!=32) { return NULL; }

		guidHash=BG_hashword((const uint32_t*)guid, 8, 0);

		user=DB_GetUserNode(guidHash, guid);

		if(!user) { return NULL; }

		data = &user->user->user;
	}

	if( !data->pb_guid[0]) {
		return NULL;
	}

	// we gotta do this because the db info is exact length but the shrubbot may like the terminating NUL also
	return va("%.8s", &data->pb_guid[24]);
}

void G_DB_ClientDisconnect(gentity_t *ent)
{
	g_shrubbot_buffered_users_t* user=NULL;
	uint32_t guidHash;
	uint32_t i;
	char guid[32];
	time_t t;

	Sil_ClearAdminProtect( ent );

	for(i=0; i < 32 && ent->client->sess.guid[i] ;i++) {
		guid[i]=toupper(ent->client->sess.guid[i]);
	}
	if(i!=32) { return; }

	guidHash=BG_hashword((const uint32_t*)guid, 8, 0);

	user=DB_GetUserNode(guidHash, guid);

	if( !user ) {
		// it is not certain that disconnecting client is found from the database.
		// Their guids get confirmed only after they begin, and if dropped before, they are not usable
		return;
	}

	user->flags&=(~SIL_DBUSERFLAG_CONNECTED); // disconnect user

	// store for keeping in case of reconnect
	user->panzerSelfKills = ent->client->pers.panzerSelfKills;

	// persistent stats need update here because weaponstats get cleared on reconnect, if that happens, then the buffered data will lose the old values
	user->user->user.kills += user->kills;
	user->user->user.deaths += user->deaths;
	// set the buffered values to 0 to prevent += corruption, just in case
	user->kills = 0;
	user->deaths = 0;

	// aliases
	if( user->currentAlias.clean_name[0] ) {
		user->currentAlias.last_seen = level.realtime;
		user->currentAlias.time_played = user->currentAlias.last_seen - user->currentAlias.first_seen;
		G_DB_UpdateAlias(user->user->user.sil_guid, &user->currentAlias, user->user->user.guidHash);
	}

	if(!time(&t)) {
		return;
	}
	// update time when disconnects
	// (xpsave will update different time field)
	user->user->user.time=t;
}

int G_DB_SetClientIdent(gentity_t *ent, uint8_t *ident, uint8_t identLength)
{
	g_shrubbot_user_f_t *data;
	// since the ent pointer points to valid data always, we use them
	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].userData) {
		return -1;
	}
	// casting the const away
	data = (g_shrubbot_user_f_t*)g_clientSInfos[ent-g_entities].userData;
	memcpy(data->ident, ident, identLength);
	if( identLength < SIL_DB_IDENT_LENGTH ) {
		data->ident_flags |= SIL_DBIDENTFLAG_SHORT;
	} else {
		data->ident_flags &= ~SIL_DBIDENTFLAG_SHORT;
	}
	// only valid idents are stored but this is the easiest way to know it
	data->ident_flags |= SIL_DBIDENTFLAG_VALID;
	return 0;
}

/*
	Checks the ident string and clears it if it is not usable for identifying players.

	@param identStr in, The C string of the client MAC address out, still the MAC string or empty if unusable
*/
void G_DB_ValidateClientIdentStringForUse( char *identStr )
{
	qboolean exclude = qfalse;

	if( strlen( identStr ) == SIL_DB_IDENTSTRING_LENGTH ) {
		// bad ident from 0.6.0, this can come from valid data also, but i suppose it is rare enough
		if( !Q_stricmpn(identStr, "0000000023C34600", SIL_DB_IDENTSTRING_LENGTH) ) {
			exclude = qtrue;
		}
	} else {
		// urgh, hard coding, actually I would like to make a config file where one could put all the MAC addresses that are unusable
		if( !Q_stricmpn(identStr, "000C29", 6) ) {
			exclude = qtrue;
		} else if( !Q_stricmpn(identStr, "005056", 6) ) {
			exclude = qtrue;
		}
	}

	if( exclude == qtrue ) {
		identStr[0] = '\0';
	}
}

void G_DB_GetClientIdentString(g_shrubbot_user_handle_t *handle, char *dest, uint32_t maxsize)
{
	int i;
	int maxsizePerTwo;

	// always return valid C string
	if( maxsize ) {
		dest[0] = '\0';
	} else {
		return;
	}

	if( !handle || !handle->user || !dest) {
		return;
	}
	if( !(handle->user->ident_flags & SIL_DBIDENTFLAG_VALID) ) {
		return;
	}

	if( handle->user->ident_flags & SIL_DBIDENTFLAG_SHORT ) {
		maxsizePerTwo = (maxsize - 1) / 2; // to ensure the overflowing strcat doesn't close server
		for(i=0; i < SIL_DB_IDENT_LENGTH_OLD && i < maxsizePerTwo ; i++) {
			Q_strcat(dest, maxsize, va("%02X", handle->user->ident[i]));
		}
	} else {
		maxsizePerTwo = (maxsize - 1) / 2; // to ensure the overflowing strcat doesn't close server
		for(i=0; i < SIL_DB_IDENT_LENGTH && i < maxsizePerTwo ; i++) {
			Q_strcat(dest, maxsize, va("%02X", handle->user->ident[i]));
		}
	}
	dest[maxsize-1] = '\0';

	// make sure only usable idents are received
	G_DB_ValidateClientIdentStringForUse(dest);
}

void G_DB_SetMuteData(gentity_t *ent, const char *reason, const char * mutedby)
{
	g_shrubbot_userextra_f_t *userExt;
	g_shrubbot_user_f_t *user;

	// since the ent pointer points to valid data always, we use them
	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].userData) {
		return;
	}

	// check if user already has extras and if not, then create them
	user = (g_shrubbot_user_f_t*) g_clientSInfos[ent-g_entities].userData;
	userExt = (g_shrubbot_userextra_f_t*) g_clientSInfos[ent-g_entities].extraData;

	if( !userExt ) {
		userExt = DB_CreateUserExtras(user);
		if( DB_AppendNewExtras(userExt) == 0 ) {
			// set the appropriate pointers for the game, in case the admin decides to reconnect during the map the keys are found
			g_clientSInfos[ent-g_entities].extraData = userExt;
		}
	}

	if( reason ) {
		Q_strncpyz(userExt->mute_reason, reason, sizeof(userExt->mute_reason));
	}

	if( mutedby ) {
		memcpy(userExt->muted_by, mutedby, sizeof(userExt->muted_by));
	} else {
		// if null, reference the mod itself as the maker
		memcpy(userExt->muted_by, SILENT_REF_GUID, sizeof(userExt->muted_by));
	}

	// don't lose this stuff during intermission/warmup
	user->mutetime = ent->client->sess.auto_unmute_time;
}

g_shrubbot_mutedata_t* G_DB_GetMuteData(gentity_t *ent)
{
	static g_shrubbot_mutedata_t mutedata;
	g_shrubbot_userextra_f_t *userExt;

	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].extraData) {
		return NULL;
	}

	userExt = (g_shrubbot_userextra_f_t*) g_clientSInfos[ent-g_entities].extraData;

	if( userExt->mute_reason[0] ) {
		Q_strncpyz(mutedata.reason, userExt->mute_reason, sizeof(mutedata.reason));
	} else {
		mutedata.reason[0] = '\0';
	}
	if( userExt->muted_by[0] ) {
		if( !Q_stricmpn(userExt->muted_by, SILENT_REF_GUID, sizeof(userExt->muted_by)) ) {
			Q_strncpyz(mutedata.mutedBy, "silEnT", sizeof(mutedata.mutedBy));
			mutedata.muteEntityPlayer = 0;
		} else if( !Q_stricmpn(userExt->muted_by, SILENT_CONSOLE_GUID, sizeof(userExt->muted_by)) ) {
			Q_strncpyz(mutedata.mutedBy, "Console", sizeof(mutedata.mutedBy));
			mutedata.muteEntityPlayer = 0;
		} else {
			uint32_t guidHash = BG_hashword((const uint32_t*)userExt->muted_by, 8, 0);
			g_shrubbot_user_f_t *user = DB_GetUserNodeWithoutBuffering(guidHash, userExt->muted_by);

			if( user && user->name[0] ) {
				Q_strncpyz(mutedata.mutedBy, user->name, sizeof(mutedata.mutedBy));
				mutedata.muteEntityPlayer = 1;
			} else {
				Q_strncpyz(mutedata.mutedBy, "*unknown*", sizeof(mutedata.mutedBy));
				mutedata.muteEntityPlayer = 0;
			}
		}
	} else {
		Q_strncpyz(mutedata.mutedBy, "*unknown*", sizeof(mutedata.mutedBy));
		mutedata.muteEntityPlayer = 0;
	}

	return &mutedata;
}

void G_DB_RemoveMuteData(gentity_t *ent)
{
	g_shrubbot_userextra_f_t *userExt;

	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].extraData) {
		return;
	}

	userExt = (g_shrubbot_userextra_f_t*) g_clientSInfos[ent-g_entities].extraData;

	memset(userExt->mute_reason, 0, sizeof(userExt->mute_reason));
	memset(userExt->muted_by, 0, sizeof(userExt->muted_by));

	if( g_clientSInfos[ent-g_entities].userData ) {
		// don't lose this stuff during intermission/warmup
		((g_shrubbot_user_f_t*) g_clientSInfos[ent-g_entities].userData)->mutetime = 0;
	}
}

static qboolean G_DB_UserWithPBGUIDExists(uint32_t guidHash, const char* pbguid)
{
	g_shrubbot_buffered_users_t *node;

	node = DB_BufferedUserNodePB(guidHash, pbguid);
	if( node ) {
		if( !(node->user->action & SIL_SHRUBBOT_DB_ACTION_REMOVE) ) {
			return qtrue;
		} else {
			// record is set to be removed so treated as non existent, only cold crash can prevent the delete
			return qfalse;
		}
	} else {
		int32_t users = usercount_onmemory;
		int32_t i;

		for(i=0; i < users ;i++) {
			if( guidHash == user_cache[i].user.pbgHash && !Q_strncmp(user_cache[i].user.pb_guid, pbguid, SIL_SHRUBBOT_DB_GUIDLEN) ) {
				if( user_cache[i].action & SIL_SHRUBBOT_DB_ACTION_REMOVE ) {
					// treat to be deleted record as nonexistent
					return qfalse;
				} else {
					return qtrue;
				}
			}
		}
	}

	return qfalse;
}

int G_DB_UpdatePunkBusterGUID(gentity_t *ent)
{
	g_shrubbot_user_f_t *data;
	uint32_t guidHash;
	int i;
	char userinfo[MAX_INFO_STRING];
	char guid[SIL_DB_KEYSIZE];
	char *pb_guid;
	// since the ent pointer points to valid data always, we use them
	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].userData) {
		return -3;
	}
	// casting the const away
	data = (g_shrubbot_user_f_t*)g_clientSInfos[ent-g_entities].userData;

	// we connect the client now with the database, get the PB GUID if available
	trap_GetUserinfo( ent-g_entities, userinfo, sizeof( userinfo ) );
	pb_guid = Info_ValueForKey(userinfo, "cl_guid");

	// validate before updating garbage
	if( !G_CheckGUID(pb_guid, qfalse) ) {
		return -1;
	}

	// update
	for(i=0; i < 32 && pb_guid[i] ;i++) {
		guid[i]=toupper(pb_guid[i]);
	}
	// hash for searches
	guidHash = BG_hashword((const uint32_t*)guid, 8, 0);

	if( G_DB_UserWithPBGUIDExists(guidHash, guid) ) {
		return -2;
	}

	memcpy(data->pb_guid, guid, SIL_DB_KEYSIZE);
	data->pbgHash = guidHash;

	return 0;
}

qboolean G_DB_PunkBusterGUIDMatches(gentity_t *ent, const char *pb_guid)
{
	g_shrubbot_user_f_t *data;

	// since the ent pointer points to valid data always, we use them
	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].userData) {
		return qfalse;
	}
	// casting the const away
	data = (g_shrubbot_user_f_t*)g_clientSInfos[ent-g_entities].userData;

	if( !Q_stricmpn(data->pb_guid, pb_guid, SIL_DB_KEYSIZE) ) {
		return qtrue;
	} else {
		return qfalse;
	}
}

qboolean G_DB_UserDataTransfer(const char *dest, const char*source)
{
	g_shrubbot_user_handle_t *handle;
	g_shrubbot_user_f_t data;
	int i;

	handle = G_DB_GetUserHandleUserID(source);
	if( !handle ) {
		return qfalse;
	}

	data.level = handle->user->level;
	data.last_xp_save = handle->user->last_xp_save;
	data.kill_rating = handle->user->kill_rating;
	data.kill_variance = handle->user->kill_variance;
	data.rating = handle->user->rating;
	data.rating_variance = handle->user->rating_variance;
	data.kills = handle->user->kills;
	data.deaths = handle->user->deaths;
	for( i=0 ; i < SK_NUM_SKILLS ; i++ ) {
		data.skill[i] = handle->user->skill[i];
	}
	data.last_xp_save = handle->user->last_xp_save;

	handle = G_DB_GetUserHandleUserID(dest);
	if( !handle ) {
		return qfalse;
	}

	handle->user->level = data.level;
	handle->user->last_xp_save = data.last_xp_save;
	handle->user->kill_rating = data.kill_rating;
	handle->user->kill_variance = data.kill_variance;
	handle->user->rating = data.rating;
	handle->user->rating_variance = data.rating_variance;
	handle->user->kills = data.kills;
	handle->user->deaths = data.deaths;
	for( i=0 ; i < SK_NUM_SKILLS ; i++ ) {
		 handle->user->skill[i] = data.skill[i];
	}
	handle->user->last_xp_save = data.last_xp_save;

	G_DB_SaveShrubbotUser( handle );

	return qtrue;
}

int G_DB_GetClientLevel(gentity_t *ent)
{
	// since the ent pointer points to valid data always, we use them
	if(!ent || !ent->client || !g_clientSInfos[ent-g_entities].userData) {
		return 0;
	}

	return g_clientSInfos[ent-g_entities].userData->level;
}

void G_DB_RefreshEntityPointers(void)
{
	gentity_t *ent;
	int i;

	for(i=0; i<level.maxclients; i++) {
		ent = &g_entities[i];
		if(ent && ent->client
			&& ent->client->pers.connected == CON_CONNECTED
			&& ent->client->sess.guid[0]) {
				g_clientSInfos[ent-g_entities].extraData = DB_FindUserExtras(g_clientSInfos[ent-g_entities].userData);
		}
	}
}


g_shrubbot_user_handle_t* G_DB_GetUserHandle(const char* guid)
{
	g_shrubbot_buffered_users_t *node=NULL;
	uint32_t					guidHash;
	uint32_t					i;
	char guid_l[32];

	for(i=0; i < 32 && guid[i] ;i++) {
		guid_l[i]=toupper(guid[i]);
	}
	if(i!=32) { return NULL; }

	guidHash=BG_hashword((const uint32_t*)guid_l, 8, 0);

	node=DB_GetUserNode(guidHash, guid_l);

	if(!node) { return NULL; }

	//handle=(g_shrubbot_user_handle_t*)malloc(sizeof(g_shrubbot_user_handle_t));

	//if(!handle) { return NULL; }

	DB_FillHandle(node, &handle_out);

	return &handle_out;
}

g_shrubbot_user_handle_t* G_DB_GetUserHandlePB(const char* guid)
{
	g_shrubbot_buffered_users_t *node=NULL;
	uint32_t					i;
	uint32_t					guidHash;
	char guid_l[32];

	for(i=0; i < 32 && guid[i] ;i++) {
		guid_l[i]=toupper(guid[i]);
	}
	if(i!=32) { return NULL; }

	guidHash = BG_hashword((const uint32_t*)guid_l, 8, 0);

	node=DB_GetUserNodePB(guidHash, guid_l);

	if(!node) { return NULL; }

	DB_FillHandle(node, &handle_out);

	return &handle_out;
}

g_shrubbot_user_handle_t* G_DB_GetUserHandleWithoutBuffering(const char* guid)
{
	g_shrubbot_user_f_t			*node = NULL;
	uint32_t					guidHash;
	uint32_t					i;
	char guid_l[32];

	for(i=0; i < 32 && guid[i] ;i++) {
		guid_l[i] = toupper(guid[i]);
	}

	if( i != 32 ) {
		return NULL;
	}

	guidHash = BG_hashword((const uint32_t*)guid_l, 8, 0);

	node = DB_GetUserNodeWithoutBuffering(guidHash, guid_l);

	if( !node ) {
		return NULL;
	}

	DB_FillHandleFromFileRecord(node, &handle_out);

	return &handle_out;
}

g_shrubbot_user_handle_t* G_DB_CreateUserRecord(const char* sil_guid, const char* pbguid)
{
	g_shrubbot_buffered_users_t *node = NULL;
	g_shrubbot_usercache_t		newuser;
	uint32_t guidHash = 0;
	uint32_t sil_guidHash = 0;
	uint32_t i;
	char guid[32];
	char sguid[32];

	// function must check the input values and return NULL if neither one is good
	// we first attempt to find using the silent guid
	if( sil_guid && sil_guid[0] ) {
		for(i=0; i < 32 && sil_guid[i] ;i++) {
			sguid[i]=toupper(sil_guid[i]);
		}
		if( i==32 ) {
			sil_guidHash = BG_hashword((const uint32_t*)sguid, 8, 0);
		}
	}
	if( pbguid && pbguid[0] ) {
		for(i=0; i < 32 && pbguid[i] ;i++) {
			guid[i]=toupper(pbguid[i]);
		}

		if( i == 32 ) {
			guidHash = BG_hashword((const uint32_t*)guid, 8, 0);
		}
	}
	if( !sil_guidHash && !guidHash ) {
		return NULL;
	}

	newuser.action=SIL_SHRUBBOT_DB_ACTION_NONE;
	newuser.filePosition=0;
	memset(&newuser.user, 0, sizeof(g_shrubbot_user_f_t));
	if( sil_guidHash ) {
		memcpy(newuser.user.sil_guid, sguid, SIL_SHRUBBOT_DB_GUIDLEN);
		newuser.user.guidHash = sil_guidHash;
		// newly created must be valid guids
		newuser.user.ident_flags |= SIL_DBGUID_VALID;
	}
	if( guidHash ) {
		memcpy(newuser.user.pb_guid, guid, SIL_SHRUBBOT_DB_GUIDLEN);
		newuser.user.pbgHash = guidHash;
	}
	newuser.user.rating_variance = SIGMA2_THETA;
	newuser.user.kill_variance = SIGMA2_DELTA;
	node = DB_BufferUserNode(&newuser, -1);

	// return values
	DB_FillHandle(node, &handle_out);

	return &handle_out;
}

qboolean G_DB_GetUserHandleLocal(const char* guid, g_shrubbot_user_handle_t* handle)
{
	g_shrubbot_buffered_users_t *node=NULL;
	uint32_t					guidHash;
	uint32_t					i;
	char guid_l[32];

	for(i=0; i < 32 && guid[i] ;i++) {
		guid_l[i]=toupper(guid[i]);
	}
	if(i!=32) { return qfalse; }
	if(!handle) { return qfalse; }

	guidHash=BG_hashword((const uint32_t*)guid_l, 8, 0);

	node=DB_GetUserNode(guidHash, guid_l);

	if(!node) { return qfalse; }

	DB_FillHandle(node, handle);

	return qtrue;
}

// We're not too concerned of the performance of this command.
// It's only available to high admins and they use it for maintenance only
g_shrubbot_user_handle_t* G_DB_GetUserHandleUserID(const char* userid)
{
	g_shrubbot_buffered_users_t *node=user_buffer;
	uint32_t					users=0, i=0;
	// loop through buffered
	while(node) {
		if(!Q_stricmpn(userid, node->user->userid, SIL_SHRUBBOT_USERID_SIZE)) {
			break;
		}
		node=node->next;
	}
	if(node) {
		//handle=(g_shrubbot_user_handle_t*)malloc(sizeof(g_shrubbot_user_handle_t));
		//if(!handle) { return NULL; }
		DB_FillHandle(node, &handle_out);
		return &handle_out;
	}
	// loop through memorycache
	users=usercount_onmemory;
	for(i=0; i<users ;i++) {
		if(DB_IsRemoved(i)) {
			continue;
		}
		if(!Q_stricmpn(user_cache[i].userid, userid, SIL_SHRUBBOT_USERID_SIZE)) {
			//handle=(g_shrubbot_user_handle_t*)malloc(sizeof(g_shrubbot_user_handle_t));
			handle_out.flags = SIL_DBUSERFLAG_CACHED;
			handle_out.node = (void*)&user_cache[i];
			handle_out.user = &user_cache[i].user;
			handle_out.userid = &user_cache[i].user.sil_guid[24];
			handle_out.shortPBGUID = &user_cache[i].user.pb_guid[24];
			return &handle_out;
		}
	}

	return NULL;
}

g_shrubbot_user_handle_t* G_DB_GetUserHandleUserIDPB(const char* userid)
{
	g_shrubbot_buffered_users_t *node=user_buffer;
	uint32_t					users=0, i=0;
	// loop through buffered
	while(node) {
		if( !Q_stricmpn(userid, node->user->shortPBGUID, SIL_SHRUBBOT_USERID_SIZE) ) {
			break;
		}
		node=node->next;
	}
	if(node) {
		//handle=(g_shrubbot_user_handle_t*)malloc(sizeof(g_shrubbot_user_handle_t));
		//if(!handle) { return NULL; }
		DB_FillHandle(node, &handle_out);
		return &handle_out;
	}
	// loop through memorycache
	users=usercount_onmemory;
	for(i=0; i<users ;i++) {
		if(DB_IsRemoved(i)) {
			continue;
		}
		if( !Q_stricmpn(user_cache[i].shortPBGUID, userid, SIL_SHRUBBOT_USERID_SIZE) ) {
			//handle=(g_shrubbot_user_handle_t*)malloc(sizeof(g_shrubbot_user_handle_t));
			handle_out.flags = SIL_DBUSERFLAG_CACHED;
			handle_out.node = (void*)&user_cache[i];
			handle_out.user = &user_cache[i].user;
			handle_out.userid = &user_cache[i].user.sil_guid[24];
			handle_out.shortPBGUID = &user_cache[i].user.pb_guid[24];
			return &handle_out;
		}
	}

	return NULL;
}

void G_DB_FreeUserHandle(g_shrubbot_user_handle_t *handle)
{
	// this is a NULL function with a cached handle
	return;
}

void G_DB_UpdateFromHandle(g_shrubbot_user_handle_t* handle, qboolean freehandle)
{
	g_shrubbot_buffered_users_t *node=NULL;

	if( !handle || (handle->flags & SIL_DBUSERFLAG_CACHED) ) {
		// Use G_DB_SaveShrubbotUser instead for records that are not buffered
		return;
	}

	node=(g_shrubbot_buffered_users_t*)handle->node;

	if(!node) {
		G_LogPrintf("G_DB_UpdateFromHandle, node==NULL\n");
		return;
	}

	node->allies_time = handle->allies_time;
	node->axis_time = handle->axis_time;
	node->kills = handle->kills;
	node->deaths = handle->deaths;
	node->hits = handle->hits;
	node->team_hits = handle->team_hits;
	//node->flags=handle->flags; flags arent edited in the outside
	node->diff_percent_time = handle->diff_percent_time;
	node->total_percent_time = handle->total_percent_time;

	if(freehandle) {
		// the handle given out is static memory
	}
}

////////////////////////////////////////////////////////////////////////////////
//
//  Buffer iteration for disconnected or connected clients
//

static qboolean DB_DisconnectedNext(g_shrubbot_buffered_users_t *users, g_shrubbot_user_handle_t* handle)
{
	while(users) {
		if(!(users->flags & SIL_DBUSERFLAG_CONNECTED)) {
			break;
		}
		users=users->next;
	}

	if(users && handle) {
		DB_FillHandle(users,handle);
		return qtrue;
	}

	return qfalse;
}

static qboolean DB_ConnectedNext(g_shrubbot_buffered_users_t *users, g_shrubbot_user_handle_t* handle)
{
	while(users) {
		if(users->flags & SIL_DBUSERFLAG_CONNECTED) {
			break;
		}
		users=users->next;
	}

	if(users && handle) {
		DB_FillHandle(users,handle);
		return qtrue;
	}

	return qfalse;
}

qboolean G_DB_GetFirstDisconnected(g_shrubbot_user_handle_t* handle)
{
	disconnected_iterator=user_buffer;

	return DB_DisconnectedNext(disconnected_iterator,handle);
}

qboolean G_DB_GetNextDisconnected(g_shrubbot_user_handle_t* handle)
{
	if(disconnected_iterator!=NULL) {
		disconnected_iterator=disconnected_iterator->next;
	} else {
		return qfalse;
	}

	return DB_DisconnectedNext(disconnected_iterator,handle);
}

qboolean G_DB_GetFirstConnected(g_shrubbot_user_handle_t* handle)
{
	disconnected_iterator=user_buffer;

	return DB_ConnectedNext(disconnected_iterator,handle);
}

qboolean G_DB_GetNextConnected(g_shrubbot_user_handle_t* handle)
{
	if(disconnected_iterator!=NULL) {
		disconnected_iterator=disconnected_iterator->next;
	} else {
		return qfalse;
	}

	return DB_ConnectedNext(disconnected_iterator,handle);
}

////////////////////////////////////////////////////////////////////////////////

void G_DB_SaveShrubbotUser(g_shrubbot_user_handle_t* handle)
{
	g_shrubbot_buffered_users_t *node=NULL;
	g_shrubbot_usercache_t *user=NULL;
	int newUser=0;

	if(!db_users_info.usable) { return; }

	if(!handle) { return; }

	if(handle->flags == SIL_DBUSERFLAG_CACHED) {
		// in this case any other flag has no meaning
		user=(g_shrubbot_usercache_t*)handle->node;
	} else {
		node=(g_shrubbot_buffered_users_t*)handle->node;
		if(!node) { return; }
		user=(g_shrubbot_usercache_t*)node->user;
	}

	if(!user) {
		G_LogPrintf("G_DB_SaveShrubbotUser, user==NULL\n");
		return;
	}

	// to make sure we don't interfere with the XP save, we need to store the current XP and save
	// the node with what it would be if the user would get XP reseted and then restore the XP
	// data to user. I'm leaving this undone. Reason: I don't relly care much about XP
	// The above is needed because i want to use the same function with user writing
	//db_users_info.db_file=fopen(file,"r+b");
	G_DB_File_Open(&db_users_info.db_file, DB_USERS_FILENAME, DB_FILEMODE_UPDATE);
	DB_WriteUserToDB(user,&newUser);
	if(newUser) {
		DB_Write_UserDBheader(db_users_info.db_file);
	}
	G_DB_File_Close(&db_users_info.db_file);
}

qboolean G_DB_ResetPlayerStats(const char *guid_short)
{
	g_shrubbot_buffered_users_t *users_b=user_buffer;
	uint32_t i,users;

	if(db_users_info.usable==qfalse) {
		return qfalse;
	}

	// buffered
	while(users_b) {
		if(!memcmp(users_b->user->userid,guid_short,SIL_SHRUBBOT_USERID_SIZE)) {
			users_b->user->user.rating_variance=SIGMA2_THETA;
			users_b->user->user.rating=0.0f;
			users_b->user->user.kill_variance=SIGMA2_DELTA;
			users_b->user->user.kill_rating=0.0f;
			users_b->user->user.deaths=0;
			users_b->user->user.kills=0;
			return qtrue;
		}
		users_b=users_b->next;
	}

	// loop through memorycache
	users=usercount_onmemory;
	for(i=0; i<users ;i++) {
		if(!memcmp(user_cache[i].userid,guid_short,SIL_SHRUBBOT_USERID_SIZE)) {
			user_cache[i].user.rating_variance=SIGMA2_THETA;
			user_cache[i].user.rating=0.0f;
			user_cache[i].user.kill_variance=SIGMA2_DELTA;
			user_cache[i].user.kill_rating=0.0f;
			user_cache[i].user.deaths=0;
			user_cache[i].user.kills=0;
			db_users_info.truncate=qtrue;
			return qtrue;
		}
	}

	return qfalse;
}

//
// Function resets XP values of the buffered clients either to what is found from the file
// or to just zero.
// Function is safe to be called even if there isn't usable DB
void G_DB_ResetXPValuesBuffer(void)
{
	g_shrubbot_buffered_users_t *users=user_buffer;
	g_shrubbot_usercache_t usercache;
	g_shrubbot_user_f_t *user; // just one more user named variable
	int i;

	if(!db_users_info.usable) {
		return;
	}

	G_DB_File_Open(&db_users_info.db_file, DB_USERS_FILENAME, DB_FILEMODE_READ);

	while(users) {
		user=&users->user->user;
		users->hits=0;
		users->team_hits=0;
		users->allies_time=0;
		users->axis_time=0;
		users->diff_percent_time=0;
		users->total_percent_time=0;

		if(users->memoryIndex!=-1) {
			// read reset values from file
			// damn, we cant reset from on memory, the on cache is shared between the buffer
			// for the worse, we cant just read the user, values that are not from XP save may
			// have been edited
			usercache.filePosition=users->user->filePosition;
			DB_ReadUserFromFile(&usercache);
			// copy data
			user->kill_rating=usercache.user.kill_rating;
			user->kill_variance=usercache.user.kill_variance;
			user->rating=usercache.user.rating;
			user->rating_variance=usercache.user.rating_variance;
			for(i=0;i<SK_NUM_SKILLS;i++) {
				user->skill[i]=usercache.user.skill[i];
			}
		} else {
			// just zeroing the values
			user->kill_rating=0.0f;
			user->kill_variance=SIGMA2_DELTA;
			user->rating=0.0f;
			user->rating_variance=SIGMA2_THETA;
			for(i=0;i<SK_NUM_SKILLS;i++) {
				user->skill[i]=0;
			}
		}
		users=users->next;
	}

	// again safe to be called anytime
	G_DB_File_Close(&db_users_info.db_file);
}

//
// Function resets XP rating of all users
// Function is safe to be called even if there isn't usable DB
void G_DB_ResetXPRatingAll(void)
{
	g_shrubbot_buffered_users_t *users=user_buffer;
	int cachesz,i;

	// on memory cache
	cachesz=usercount_onmemory;
	for(i=0;i<cachesz;i++) {
		user_cache[i].user.kill_rating=0.0f;
		user_cache[i].user.kill_variance=SIGMA2_DELTA;
		user_cache[i].user.rating=0.0f;
		user_cache[i].user.rating_variance=SIGMA2_THETA;
	}

	// buffer instances that are not in cache
	while(users) {
		if(users->memoryIndex==-1) {
			users->user->user.kill_rating=0.0f;
			users->user->user.kill_variance=SIGMA2_DELTA;
			users->user->user.rating=0.0f;
			users->user->user.rating_variance=SIGMA2_THETA;
		}
		users=users->next;
	}
	// the file must be written for the updates to take effect on offline players
	db_users_info.truncate=qtrue;
}

//
// Function resets the XP of all users
// Function is safe to be called even if there isn't usable DB
void G_DB_ResetXPAll(void)
{
	g_shrubbot_buffered_users_t *users=user_buffer;
	int cachesz, i; //,j;

	// on memory cache
	cachesz=usercount_onmemory;
	for(i=0;i<cachesz;i++) {
		memset(user_cache[i].user.skill, 0, sizeof(user_cache[i].user.skill));
		//for(j=0; j<SK_NUM_SKILLS; j++) {
		//	user_cache[i].user.skill[j] = 0.0f;
		//}
	}

	// buffer instances that are not in cache
	while(users) {
		users->hits=0;
		users->team_hits=0;
		users->allies_time=0;
		users->axis_time=0;
		users->diff_percent_time=0;
		users->total_percent_time=0;
		if(users->memoryIndex==-1) {
			memset(users->user->user.skill, 0, sizeof(users->user->user.skill));
			//for(j=0; j<SK_NUM_SKILLS; j++) {
			//	users->user->user.skill[j] = 0.0f;
			//}
		}
		users=users->next;
	}
	db_users_info.truncate=qtrue;
}

//
// Reset the stored total stats for all users. Not the session specifics.
void G_DB_ResetStatsAll(void)
{
	g_shrubbot_buffered_users_t *users=user_buffer;
	int cachesz,i;

	// on memory cache
	cachesz=usercount_onmemory;
	for(i=0;i<cachesz;i++) {
		user_cache[i].user.kills=0;
		user_cache[i].user.deaths=0;
	}

	// buffer instances that are not in cache
	while(users) {
		users->user->user.kills=0;
		users->user->user.deaths=0;
		users=users->next;
	}
	db_users_info.truncate=qtrue;
}

void G_DB_SaveOnMemory()
{
	if(db_users_info.usable==qfalse) {
		return;
	}

	DB_WriteExtrasToDB(qfalse);

	if(db_users_info.truncate==qtrue) {
		// the files get rewritten anyway
		return;
	}

	DB_WriteUsersToDB(qfalse);
}

uint32_t G_DB_GetUsercount(void)
{
	return (usercount_onlybuffer+usercount_onmemory);
}

qboolean G_DB_SetIterator(uint32_t start)
{
	uint32_t pos = 1;

	buffer_iterator = user_buffer;
	cache_iterator = 0;

	if( start == 0 ) {
		start++;
	}

	// skip the buffer if we can
	if( start > usercount_buffer ) {
		// the cache needs to be iterated, there are so many skip possibilities
		pos += usercount_buffer;
		buffer_iterator = NULL;
		if( (start - usercount_onlybuffer) > usercount_onmemory ) {
			return qfalse; // out of bounds return, from now on we wont list empty pages
		}
		// notice, theres no need to iterate to the first that is printed
		// as long as it is iterated past the last that would be printed
		// before start
		while( (pos < start) && (cache_iterator < usercount_onmemory) ) {
			cache_iterator++;
			if( DB_IsInBuffer(cache_iterator) ) {
				continue;
			} else if( DB_IsRemoved(cache_iterator) ) {
				continue;
			}
			pos++;
		}
		if( pos == start ) {
			return qtrue;
		}
	} else {
		// it's in the buffer
		while( (pos < start) && buffer_iterator ) {
			buffer_iterator = buffer_iterator->next;
			pos++;
		}
		if( buffer_iterator ) {
			return qtrue;
		}
	}

	return qfalse;
}

qboolean G_DB_GetIteratedUser(g_shrubbot_user_handle_t *handle)
{
	if(buffer_iterator) {
		DB_FillHandle(buffer_iterator,handle);
		buffer_iterator=buffer_iterator->next;
		return qtrue;
	} else {
		while(cache_iterator < usercount_onmemory && (DB_IsInBuffer(cache_iterator) || DB_IsRemoved(cache_iterator))) {
			cache_iterator++;
		}
		if(cache_iterator < usercount_onmemory) {
			handle->user = &user_cache[cache_iterator].user;
			handle->userid = &handle->user->sil_guid[24];
			handle->shortPBGUID = &handle->user->pb_guid[24];
			handle->node = (void*)&user_cache[cache_iterator];
			handle->flags = SIL_DBUSERFLAG_CACHED;
			cache_iterator++;
			return qtrue;
		}
	}

	return qfalse;
}

qboolean G_DB_DeleteUser(const char *guid_short)
{
	g_shrubbot_buffered_users_t *users_b=user_buffer;
	g_shrubbot_userextras_appendbuffer_t *appends = append_buffer;
	g_shrubbot_userextras_cache_t *extra=NULL;
	uint32_t i,users;

	if(db_users_info.usable==qfalse) {
		return qfalse;
	}

	// buffered
	while(users_b) {
		if(!memcmp(users_b->user->userid, guid_short, SIL_SHRUBBOT_USERID_SIZE)) {
			users_b->user->action=SIL_SHRUBBOT_DB_ACTION_REMOVE;
			while(appends) {
				if(!Q_stricmpn(appends->user->sil_guid, users_b->user->user.sil_guid, SIL_SHRUBBOT_DB_GUIDLEN)) {
					appends->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
					break;
				}
				appends = appends->next;
			}
			db_users_info.truncate=qtrue;
			// aliases
			G_DB_RemoveAliases(users_b->user->user.sil_guid, users_b->user->user.guidHash);
			return qtrue;
		}
		users_b=users_b->next;
	}

	// loop through memorycache
	users=usercount_onmemory;
	for(i=0; i<users ;i++) {
		if(!memcmp(user_cache[i].userid,guid_short,SIL_SHRUBBOT_USERID_SIZE)) {
			user_cache[i].action=SIL_SHRUBBOT_DB_ACTION_REMOVE;
			extra = DB_FindExtrasCacheData(user_cache[i].user.sil_guid);
			if(extra) {
				extra->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			}
			db_users_info.truncate=qtrue;
			// aliases
			G_DB_RemoveAliases(user_cache[i].user.sil_guid, user_cache[i].user.guidHash);
			return qtrue;
		}
	}

	return qfalse;
}

qboolean G_DB_DeleteUserPB(const char *guid_short)
{
	g_shrubbot_buffered_users_t *users_b=user_buffer;
	g_shrubbot_userextras_appendbuffer_t *appends = append_buffer;
	g_shrubbot_userextras_cache_t *extra=NULL;
	uint32_t i,users;

	if(db_users_info.usable==qfalse) {
		return qfalse;
	}

	// buffered
	while(users_b) {
		if( !memcmp(users_b->user->shortPBGUID, guid_short, SIL_SHRUBBOT_USERID_SIZE) ) {
			users_b->user->action=SIL_SHRUBBOT_DB_ACTION_REMOVE;
			while(appends) {
				if(!Q_stricmpn(appends->user->pb_guid, users_b->user->user.pb_guid, SIL_SHRUBBOT_DB_GUIDLEN)) {
					appends->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
					break;
				}
				appends = appends->next;
			}
			db_users_info.truncate=qtrue;
			return qtrue;
		}
		users_b=users_b->next;
	}

	// loop through memorycache
	users=usercount_onmemory;
	for(i=0; i<users ;i++) {
		if(!memcmp(user_cache[i].shortPBGUID, guid_short, SIL_SHRUBBOT_USERID_SIZE) ) {
			user_cache[i].action=SIL_SHRUBBOT_DB_ACTION_REMOVE;
			extra = DB_FindExtrasCacheDataPB(user_cache[i].user.pb_guid);
			if(extra) {
				extra->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			}
			db_users_info.truncate=qtrue;
			return qtrue;
		}
	}

	return qfalse;
}

void G_DB_PruneUsers(void)
{
	g_shrubbot_userextras_cache_t *extras;
	uint32_t	i;
	uint32_t	users;
	int32_t		age;
	time_t		t;
	qboolean	remove=qfalse;

	if(db_users_info.usable==qfalse) {
		return;
	}

	if(!g_dbUserMaxAge.string[0] || !g_dbUserMaxAge.integer) {
		return;
	}
	if(!time(&t)) {
		return;
	}
	// iterate all on memory users

	age=DB_Get_UserMaxAge();

	users=usercount_onmemory;
	for(i=0; i < users ;i++) {
		// we skip users who have not appeared on the server
		// this can happen when admin reads the admin.cfg
		if(!user_cache[i].user.time) {
			continue;
		}
		/* Not deleting unlinkables here, those can be used to create bans and stuff still
		if( !user_cache[i].user.pbgHash && !(user_cache[i].user.ident_flags & SIL_DBGUID_VALID) ) {
			user_cache[i].action=SIL_SHRUBBOT_DB_ACTION_REMOVE;
			extras = DB_FindExtrasCacheData(user_cache[i].user.sil_guid);
			if( extras ) {
				extras->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			}
			remove=qtrue;
			continue;
		}*/
		if((t - user_cache[i].user.time) > age) {
			user_cache[i].action=SIL_SHRUBBOT_DB_ACTION_REMOVE;
			extras = DB_FindExtrasCacheData(user_cache[i].user.sil_guid);
			if(!extras && user_cache[i].user.pb_guid[0]) {
				extras = DB_FindExtrasCacheDataPB(user_cache[i].user.pb_guid);
			}
			if( extras ) {
				extras->action = SIL_SHRUBBOT_DB_ACTION_REMOVE;
			}
			G_DB_RemoveAliases(user_cache[i].user.sil_guid, user_cache[i].user.guidHash);
			remove=qtrue;
		}
	}
	// mark the old ones for deletion
	// mark the truncate if necessary
	if(remove) {
		db_users_info.truncate=qtrue;
	}
}

//
// Database searching

// returns true if, IPs are the same
//                 the IPs are the same but compare_to is longer
// returns false if, IP is shorter
//                 the IPs are not the same
static qboolean DB_IPFits(const char* compare_to, const char* IP)
{
	while(*compare_to) {
		if(!*IP) {
			return qtrue;
		}
		if(*compare_to!=*IP) {
			return qfalse;
		}
		compare_to++;
		IP++;
	}
	if(*IP) {
		return qfalse;
	}
	return qtrue;
}

static qboolean NewSearchLoopOnMemory(const char* pattern, int32_t level, const char* IP)
{
	uint32_t	users;
	uint32_t	uindex=0;

	memset(&search_cache, 0, sizeof(search_cache));

	users=usercount_onmemory;
	for(uindex=0; uindex < users ;uindex++) {
		if(user_cache[uindex].user.name[0] && !user_cache[uindex].user.sanitized_name[0]) {
			// the sanitized name
			Q_strncpyz(user_cache[uindex].user.sanitized_name, G_DB_SanitizeName(user_cache[uindex].user.name), MAX_NAME_LENGTH);
			// mark the file dirty so the sanitized names will be saved in the future
			db_users_info.truncate=qtrue;
		}
		// discard removed records to avoid confusion after !userdel
		if( user_cache[uindex].action & SIL_SHRUBBOT_DB_ACTION_REMOVE ) {
			continue;
		}
		// discard if level wont fit
		if((level >= 0) && (level != user_cache[uindex].user.level)) {
			continue;
		}
		// discard IP if it wont fit
		if(IP[0] && !DB_IPFits(user_cache[uindex].user.ip, IP)) {
			continue;
		}
		// discard if name wont fit
		if(pattern[0] && user_cache[uindex].user.sanitized_name[0]) {
			if(strstr(user_cache[uindex].user.sanitized_name, pattern) == NULL) {
				continue;
			}
		} else if(pattern[0]){
			// skip it anyway
			continue;
		}
		if(search_cache.used_cache==SIL_SHRUBBOT_DB_MAXSEARCHCACHE) {
			return qfalse; // too many results
		}
		search_cache.results[search_cache.used_cache]=uindex;
		search_cache.used_cache++;
	}
	search_cache.usable_results=search_cache.used_cache;
	if(pattern[0]) {
		search_cache.search_type|=SIL_SHRUBBOT_DB_SEARCHNAME;
		Q_strncpyz(search_cache.search_pattern, pattern, sizeof(search_cache.search_pattern));
	}
	if(level >= 0) {
		search_cache.search_type|=SIL_SHRUBBOT_DB_SEARCHLEVEL;
		search_cache.search_level=level;
	}
	if(IP[0]) {
		search_cache.search_type|=SIL_SHRUBBOT_DB_SEARCHIP;
		Q_strncpyz(search_cache.search_ip, IP, sizeof(search_cache.search_ip));
	}

	return qtrue;
}

static void DB_NameDiscardResults(const char* pattern)
{
	uint32_t	users;
	uint32_t	cindex=0;

	users=search_cache.used_cache;
	for(cindex=0; cindex < users ;cindex++) {
		int rindex=search_cache.results[cindex];
		if(rindex==-1) {
			continue;
		}
		if(strstr(user_cache[rindex].user.sanitized_name, pattern) == NULL) {
			// remove from resultset
			search_cache.results[cindex]=-1;
			search_cache.usable_results--;
		}
	}
	search_cache.search_type|=SIL_SHRUBBOT_DB_SEARCHNAME;
	Q_strncpyz(search_cache.search_pattern, pattern, sizeof(search_cache.search_pattern));
}

static void DB_LevelDiscardResults(int32_t level)
{
	uint32_t	users;
	uint32_t	cindex=0;

	users=search_cache.used_cache;
	for(cindex=0; cindex < users ;cindex++) {
		int rindex=search_cache.results[cindex];
		if(rindex==-1) {
			continue;
		}
		if(user_cache[rindex].user.level != level) {
			// remove from resultset
			search_cache.results[cindex]=-1;
			search_cache.usable_results--;
		}
	}
	search_cache.search_type|=SIL_SHRUBBOT_DB_SEARCHLEVEL;
	search_cache.search_level=(uint32_t)level;
}

static void DB_IPDiscardResults(const char* IP)
{
	uint32_t	users;
	uint32_t	cindex=0;

	users=search_cache.used_cache;
	for(cindex=0; cindex < users ;cindex++) {
		int rindex=search_cache.results[cindex];
		if(rindex==-1) {
			continue;
		}
		if(!DB_IPFits(user_cache[rindex].user.ip, IP)) {
			// remove from resultset
			search_cache.results[cindex]=-1;
			search_cache.usable_results--;
		}
	}
	search_cache.search_type|=SIL_SHRUBBOT_DB_SEARCHIP;
	Q_strncpyz(search_cache.search_ip, IP, sizeof(search_cache.search_ip));
}

// return 2 if exact match, 1 if usable, 0 if not
static uint32_t DB_PatternUsable(const char* new)
{
	uint32_t length1;
	uint32_t length2;

	if(!(search_cache.search_type & SIL_SHRUBBOT_DB_SEARCHNAME)) {
		if(new[0]) {
			return 1;
		} else {
			return 2;
		}
	}

	length1=strlen(search_cache.search_pattern);
	length2=strlen(new);

	if(length1 == length2) {
		if(!Q_strncmp(search_cache.search_pattern, new, length1)) {
			return 2;
		}
	} else if(length1 < length2) {
		if(strstr(new, search_cache.search_pattern)) {
			return 1;
		}
	}
	return 0;
}

// return 2 if exact match, 1 if usable, 0 if not
static uint32_t DB_LevelUsable(int32_t level)
{
	if(!(search_cache.search_type & SIL_SHRUBBOT_DB_SEARCHLEVEL)) {
		if(level >= 0) {
			return 1;
		} else {
			return 2;
		}
	}
	if(!(level>=0) && (search_cache.search_type & SIL_SHRUBBOT_DB_SEARCHLEVEL)) {
		return 0;
	}
	if((uint32_t)level == search_cache.search_level) {
		return 2;
	}
	return 0;
}

// return 2 if exact match, 1 if usable, 0 if not
static uint32_t DB_IPUsable(const char *IP)
{
	if(!(search_cache.search_type & SIL_SHRUBBOT_DB_SEARCHIP)) {
		if(IP[0]) {
			return 1;
		} else {
			return 2;
		}
	}
	if(!IP[0] && (search_cache.search_type & SIL_SHRUBBOT_DB_SEARCHIP)) {
		return 0;
	}
	if(DB_IPFits(IP, search_cache.search_ip)) {
		return 1;
	}
	return 0;
}

// cleans removed records from old resultset
static void DB_CleanOldResultSet(void)
{
	uint32_t	users;
	uint32_t	cindex=0;

	users=search_cache.used_cache;
	for(cindex=0; cindex < users ;cindex++) {
		int rindex=search_cache.results[cindex];
		if(rindex==-1) {
			continue;
		}
		if( user_cache[rindex].action & SIL_SHRUBBOT_DB_ACTION_REMOVE ) {
			search_cache.results[cindex]=-1;
			search_cache.usable_results--;
		}
	}
}

// function searches the db for given parameters
// it will internally decide whether to make clean search or use old
// results for the search
qboolean G_DB_SearchDatabase(const char* name_pattern, int32_t level, const char* IP)
{
	char		sanitized_pattern[MAX_NAME_LENGTH];
	uint32_t	pattern_usable=0;
	uint32_t	level_usable=0;
	uint32_t	ip_usable=0;

	Q_strncpyz(sanitized_pattern, G_DB_SanitizeName(name_pattern), sizeof(sanitized_pattern));

	if(search_cache.search_type) {
		// check do we use the old result set as basis for new search
		level_usable=DB_LevelUsable(level);
		pattern_usable=DB_PatternUsable(sanitized_pattern);
		ip_usable=DB_IPUsable(IP);

		if(level_usable == 2 && pattern_usable == 2 && ip_usable==2) {
			// exact match to old parameters
			DB_CleanOldResultSet();
			return qtrue;
		}
	}
	if(!level_usable || !pattern_usable || !ip_usable) {
		if(NewSearchLoopOnMemory(sanitized_pattern, level, IP)) {
			return qtrue;
		} else {
			return qfalse;
		}
	} else {
		DB_CleanOldResultSet();
		// use the old results
		if(level >= 0 && level_usable!=2) {
			DB_LevelDiscardResults(level);
		}
		if(sanitized_pattern[0] && pattern_usable!=2) {
			DB_NameDiscardResults(sanitized_pattern);
		}
		if(IP[0] && ip_usable!=2) {
			DB_IPDiscardResults(IP);
		}
	}
	return qtrue;
}

qboolean G_DB_GetResultCount(void)
{
	if(search_cache.search_type == SIL_SHRUBBOT_DB_SEARCHNONE) {
		return 0;
	}
	return search_cache.usable_results;
}

// iterates the internal pointer to the first possible result
// jumping over discarded results
qboolean G_DB_SetResultStart(uint32_t index)
{
	uint32_t i;
	uint32_t *iter=&search_cache.iterator;

	if(search_cache.search_type == SIL_SHRUBBOT_DB_SEARCHNONE) {
		return qfalse;
	}
	if(index < 1 || index > search_cache.usable_results) {
		// note, usable_results is always less or equal to used_cache
		return qfalse;
	}

	*iter=0;
	while(search_cache.results[(*iter)]==-1) {
		(*iter)++;// iterate to the first readbale
	}
	index--;
	for(i=0; i < index ; (*iter)++) {
		if(search_cache.results[(*iter)]==-1) {
			continue;
		}
		i++;
	}

	return qtrue;
}

qboolean G_DB_GetResultUser(g_shrubbot_user_handle_t *handle)
{
	g_shrubbot_usercache_t *user;
	uint32_t usedc=search_cache.used_cache;
	uint32_t *iter=&search_cache.iterator;

	if(search_cache.search_type == SIL_SHRUBBOT_DB_SEARCHNONE) {
		return qfalse;
	}
	if(*iter == usedc) {
		return qfalse;
	}

	user = &user_cache[search_cache.results[(*iter)]];
	handle->flags = SIL_DBUSERFLAG_CACHED;
	handle->node = (void*)user;
	handle->user = &user->user;
	handle->userid = &user->user.sil_guid[24];
	handle->shortPBGUID = &user->user.pb_guid[24];

	(*iter)++;
	while(*iter < usedc && search_cache.results[(*iter)]==-1) {
		(*iter)++;
	}

	return qtrue;
}

qboolean G_DB_IsWhiteListed(const char *guid)
{
	g_shrubbot_buffered_users_t* user=NULL;
	uint32_t guidHash;
	uint32_t i;
	char guid_t[32];

	for(i=0; i < 32 && guid[i] ;i++) {
		guid_t[i]=toupper(guid[i]);
	}
	if(i!=32) { return qfalse; }

	guidHash=BG_hashword((const uint32_t*)guid_t, 8, 0);

	user=DB_GetUserNode(guidHash, guid_t);

	if( user == NULL ) {
		return qfalse;
	}

	if( user->user->user.ident_flags & SIL_DBIDENTFLAG_WHITELISTED ) {
		return qtrue;
	}

	return qfalse;
}

