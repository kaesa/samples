/*****************************************************************************
 *	Module contain user database for XP saving and shrubbot levels
 *  Module is developed for silEnT - Enemy Territory game.
 *
 *  User database implementation interface.
 *
 *  User databse handles client XP save and shrubbot information. It also
 *  stores various data that is used only during the level. It is extended
 *  for that for the simplicity of the outside program. The data qualify to be
 *  saved in this module if the storing is needed regardless is the client
 *  connected during one whole level time.
 *
 *	Direct pointers to valid user data or NULL is placed to the g_entity
 *  when client connects. These pointers are updated if needed by the interface
 *  functions. Data pointed by these pointers may not be altered. Those are
 *  read only to improve some parts of the program that need to check these
 *  values very often.
 *
 *  Session specific data: hits, team_hits
 *
 *  Special note: The module is self sufficient with memory handling. You can
 *                never get a pointer you can free or store locally through
 *                this interface. This includes any pointers that are filled
 *                into any structured datatypes that are supplied from the
 *                outside program.
 *
 *  Changelog ( date, changes, author ):
 *  2010-09-24,  first version (XP save done), gaoesa
 *  2010-09-25,  Added support to write to the file in the middle of level.
 *               Added new function to fill handle data to a local variable.
 *               Added commands to support the buffered DB even without XP
 *               save being enabled.
 *               , gaoesa
 *  2010-09-26,  Added guid hashing to change string comparisons to int
 *               comparisons when looping through clients. Especially the cache.
 *               , gaoesa
 *  2010-10-20   Implementation full (XP save+shrubbot), fixes to db truncation.
 *               , gaoesa
 *  2010-10-28   Fixed a crash combination with !userinfo command, gaoesa
 *  2010-12-13   Added functionality to search through user database by name,
 *               by level or by IP. Arguments can be combined.
 *               , gaoesa
 *  2011-01-04   Removed all small memory heap allocations. For performace and
 *               to avoid memory fragmentation in server. Static memory used
 *               for memory pooling.
 *               , gaoesa
 *  2011-03-12   Added support for silEnT ident. Database divided to several
 *               files. Database data integrated more tightly to the game.
 *               This removes the need to constantly refind the player data.
 *               Interface functions must be used to alter any data in the database
 *               Database conversion from old version is done automatically.
 *               , gaoesa
 *  2011-07-05   Added buffer overflow protection into the name sanitization.
 *               , gaoesa
 *  2012-09-06   Database conversion to the 0.4 version. Added mutedata,
 *               longer ident fields, optional aliases database. Fie handling
 *               separated to independent module.
 *               , gaoesa
 *****************************************************************************/

#ifndef __G_SHRUBBOTDB_H__
#define __G_SHRUBBOTDB_H__

// Version info is used to make sure the records are compatible with the used mod version.
// It also allows making automatic db conversions.
#define DB_USERS_VERSION "SLEnT UDB v0.4\0\0"
#define DB_USERS_VERSIONSIZE 16
#define DB_USERS_FILENAME "userdb.db"

#define DB_USERSEXTRA_VERSION "SLEnT UXDB v0.4\0"
#define DB_USERSEXTRA_VERSIONSIZE 16
#define DB_USERSEXTRA_FILENAME "userxdb.db"

#define SIL_DB_GREETING_SIZE 128
#define SIL_DB_GREETING_SOUND_SIZE 256
#define SIL_DB_KEYSIZE 32

// g_dbDirectory // directory under fs_game where the database files are

#define SIL_SHRUBBOT_DB_GUIDLEN			32
#define SIL_SHRUBBOT_IPLEN				16
#define SIL_SHRUBBOT_USERID_SIZE		8
#define SIL_DB_IDENT_LENGTH				8
#define SIL_DB_IDENTSTRING_LENGTH		16
#define SIL_DB_MUTEREASONLENGTH			64

// Database user flags
//
// The flags store information that is needed internally and in the outside.
// The flags may not be edited in the outside. Only interface functions are allowed to edit
// the flag data. This is because the flags are also used to optimise operations with data
// that is routed through the outside.
#define SIL_DBUSERFLAG_CONNECTED		0x01
// SIL_DBUSERFLAG_CACHED, when flag is set, all other flags must be 0
#define SIL_DBUSERFLAG_CACHED			0x02
#define SIL_DBUSERFLAG_FULLINIT			0x04	// Player has had full connect including it's stats cleared

// DB ident flags
#define SIL_DBIDENTFLAG_VALID			0x01
#define SIL_DBIDENTFLAG_WHITELISTED		0x02	// Any of the IP bans do not effect
#define SIL_DBGUID_VALID				0x04	// The silEnT guid in the database is valid for 0.5.1 onwards
#define SIL_DBIDENTFLAG_SHORT			0x08	// The ident is made of the MAC address

// silEnT GUID for the use in case the mod is referenced
#define SILENT_REF_GUID "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
// silEnT GUID for the console
#define SILENT_CONSOLE_GUID "00000000000000000000000000000000"
// the first part of the ETTV GUID, the last 2 digits are the client number
#define SILENT_ETTV_GUID_30 "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"

#include "g_shrubbot.h"

//
// This struct holds everything that is saved in the database, and nothing more.
// Note, keep this aligned with 4, this will prevent the compiler to produce several
// instructions to access single data field (i.e. even if you need just one byte, take 4)
typedef struct g_shrubbot_user_f_s {
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
	uint8_t		ident[SIL_DB_IDENT_LENGTH];
	uint32_t	ident_flags;
	uint32_t	last_xp_save;
} g_shrubbot_user_f_t;  // size: 304 bytes for 10 000 users ~ 2,89 MB

// Extra data related to some users
// These are so rare that guid hashes are not needed.
typedef struct g_shrubbot_userextra_f_s {
	char		pb_guid[SIL_SHRUBBOT_DB_GUIDLEN];
	char		sil_guid[SIL_SHRUBBOT_DB_GUIDLEN];
	char		greeting[SIL_DB_GREETING_SIZE];
	char		greeting_sound[SIL_DB_GREETING_SOUND_SIZE];
	char		server_key[SIL_DB_KEYSIZE];
	char		client_key[SIL_DB_KEYSIZE];
	char		muted_by[SIL_SHRUBBOT_DB_GUIDLEN];
	char		mute_reason[SIL_DB_MUTEREASONLENGTH];
} g_shrubbot_userextra_f_t;

//
// This struct will deliver the user data to the outer world
// it is used so we can hide all algorithmic details inside the module
// for optional extra data, different interface functions are used
typedef struct {
	g_shrubbot_user_f_t *user;		// WARNING: never free or store locally this pointer
	const char			*userid;
	const char			*shortPBGUID;
	float				hits;
	float				team_hits;
	int32_t				axis_time;
	int32_t				allies_time;
	int32_t				kills;		// we need to keep the kills and deaths in these temporarily
	int32_t				deaths;		// until the very end. So that we won't accidentally double kills because client reconnects
	// These are temp variables for PR system usage, not really tracked
	float				total_percent_time;
	float				diff_percent_time;
	int32_t				flags;		// has internal information, do not edit in the outside
	void*				node;		// reserved for internal use, do not handle
} g_shrubbot_user_handle_t;

typedef struct g_shrubbot_mutedata_s {
	char reason[SIL_DB_MUTEREASONLENGTH];		// reason for the mute
	char mutedBy[SIL_SHRUBBOT_DB_GUIDLEN];		// the name of the player who muted
	int  muteEntityPlayer;						// is the muted by a player
} g_shrubbot_mutedata_t;


// Database initialisation and closing.
// Both must be called respectively in map init and end level.
int32_t		G_DB_InitDatabase(qboolean force);
void		G_DB_CloseDatabase(void);
void		G_DB_IssueFileOptimize(void);
void		G_DB_IssueCleanup(void);
void		G_DB_IntermissionActions(void);

// client connect and disconnect
void G_DB_ClientConnect(gentity_t *ent, char* pb_guid);
void G_DB_ClientDisconnect(gentity_t *ent);
// userinfo changed
void G_DB_SetClientName(gentity_t *ent);
qboolean G_DB_RestoreClientName(gentity_t *ent);
// update aliases for the search
void G_DB_StoreCurrentAlias(gentity_t *ent);
// updates the aliases ofall connected players, so they get included in the search
void G_DB_UpdateAliases( void );
// silEnT guid validity in the database
void G_DB_SetClientGUIDValid(g_shrubbot_user_handle_t *handle);
qboolean G_DB_IsClientGUIDValid(const g_shrubbot_user_handle_t *handle);

/**
 * Function to acquire client PunkBuster GUID. Since the silEnT players will always have silEnT GUIDs, it is safe
 * to integrate these all into the database.
 *
 * @param ent The entity whos GUID is requested
 * @return 8 character PB GUID or NULL
 */
char *G_DB_GetClientGUIDStub( gentity_t *ent );

// refreshes all entity pointers to valid database data
// needed only for the extra data with readadmins for now, expand this when needed
void G_DB_RefreshEntityPointers(void);

// Functions for extras
// Extra data may not be edited with other then interface functions
const g_shrubbot_userextra_f_t* G_DB_GetUserExtras(const g_shrubbot_user_handle_t *handle);
int G_DB_SetUserGreeting(const g_shrubbot_user_handle_t *handle, const char *greeting);
int G_DB_SetUserGreetingSound(const g_shrubbot_user_handle_t *handle, const char *greeting_sound);
int G_DB_SetUserKeys(const gentity_t *ent, const char *serverKey, const char *clientKey);
qboolean G_DB_UserHasKeys(const gentity_t *ent);

// ident data
int G_DB_SetClientIdent(gentity_t *ent, uint8_t *ident, uint8_t identLength);
void G_DB_ValidateClientIdentStringForUse( char *identStr );
void G_DB_GetClientIdentString(g_shrubbot_user_handle_t *handle, char *dest, uint32_t maxsize);

// mutes
void G_DB_SetMuteData(gentity_t *ent, const char *reason, const char * mutedby);
g_shrubbot_mutedata_t* G_DB_GetMuteData(gentity_t *ent);
void G_DB_RemoveMuteData(gentity_t *ent);

// Punk Buster GUID
/**
 * Function updates the PunkBuster GUID in the database if there doesn't already exist record
 * with that guid and the guid is valid.
 *
 * @param ent The player entity whos record gets updated
 * @return 0 - success, -1 invalid GUID, -2 dublicate GUID, -3 invalid entity
 */
int G_DB_UpdatePunkBusterGUID(gentity_t *ent);
qboolean G_DB_PunkBusterGUIDMatches(gentity_t *ent, const char *pb_guid);

// transfer player data
qboolean G_DB_UserDataTransfer(const char *dest, const char *source);

// user shrubbot level
int G_DB_GetClientLevel(gentity_t *ent);

// Database maintenance
// removing user from db
qboolean G_DB_DeleteUser(const char *guid_short);
qboolean G_DB_DeleteUserPB(const char *guid_short);

// G_DB_FreeUserHandle function must be called to all user handles that are
// received through the module interface.
void G_DB_FreeUserHandle(g_shrubbot_user_handle_t *handle);

// user record handles
// returns a user with handle struct.
g_shrubbot_user_handle_t* G_DB_GetUserHandle(const char* guid);
g_shrubbot_user_handle_t* G_DB_GetUserHandlePB(const char* guid);
g_shrubbot_user_handle_t* G_DB_GetUserHandleWithoutBuffering(const char* guid);
g_shrubbot_user_handle_t* G_DB_CreateUserRecord(const char* sil_guid, const char* pbguid);
// for faster accesses, the rules for pointers still apply
qboolean G_DB_GetUserHandleLocal(const char* guid, g_shrubbot_user_handle_t* handle);

// G_DB_UpdateFromHandle updates all the data there is in the handle to the
// buffered clients. Don't use freehandle for locally created handles.
void G_DB_UpdateFromHandle(g_shrubbot_user_handle_t* handle, qboolean freehandle);

// Iteration through all buffered and disconnected users
//
// The G_DB_GetFirst(Disconnected/Connected) will reset internal iterator to the first disconnected
// or connected client that is in the buffer. All subsequent calls to G_DB_GetNext(x) function
// will give the rest of the disconnected/connected clients. When all the clients have been returned
// exactly once, a qfalse is returned. The iterator is shared between the two function types.

// Functions return qtrue if succesfull or qfalse if there are no more disconnected clients

qboolean G_DB_GetFirstDisconnected(g_shrubbot_user_handle_t* handle);
qboolean G_DB_GetNextDisconnected(g_shrubbot_user_handle_t* handle);

// Functions return qtrue if succesfull or qfalse if there are no more disconnected clients
qboolean G_DB_GetFirstConnected(g_shrubbot_user_handle_t* handle);
qboolean G_DB_GetNextConnected(g_shrubbot_user_handle_t* handle);

// saving the buffered clients to the actual database file
// this handles also moves the temp values to the database when needed
void G_DB_SaveOnMemory(void);

// saving changes in the shrubbot things to the database (actually, for now it saves also the XP)
void G_DB_SaveShrubbotUser(g_shrubbot_user_handle_t* handle);

/**
 * Clears all statistics from specific player.
 *
 * @param guid_short 8 character GUID to identify the resetted player
 *
 * @return qboolean true if succesfull, false otherwise
 */
qboolean G_DB_ResetPlayerStats(const char *guid_short);

// in case the XP save part needs to be reset to values found from file
void G_DB_ResetXPValuesBuffer(void);
void G_DB_ResetXPRatingAll(void);
void G_DB_ResetXPAll(void);
void G_DB_ResetStatsAll(void);

// shrubbot user edit commands
/**
 * Get handle to user in database using silEnT GUID
 *
 * @param userid 8 character ending of the silEnT GUID
 *
 * @return handle to user
 */
g_shrubbot_user_handle_t* G_DB_GetUserHandleUserID(const char* userid);
/**
 * Get handle to user in database using PunkBuster GUID
 *
 * @param userid 8 character ending of the PunkBuster GUID
 *
 * @return handle to user
 */
g_shrubbot_user_handle_t* G_DB_GetUserHandleUserIDPB(const char* userid);
uint32_t G_DB_GetUsercount(void);

/**
*  Function set the internal iterator to the position that is after the last one which would be printed
*  before the given start parameter. The internal iterator may be pointing to a record that will not be printed.
*  This case is handled by the G_DB_GetIteratedUser() function that is used in combination with this function.
*  If the given start parameter is 0, it is ahndled as it was 1.
*
*  @param start The number of the record that is requested to be the first one accessed by G_DB_GetIteratedUser function.
*  @return qtrue if there will be something to get, qfalse otherwise
*/
qboolean G_DB_SetIterator(uint32_t start);
qboolean G_DB_GetIteratedUser(g_shrubbot_user_handle_t *handle);

//
// database searching
//

/**
 *  Function removes all color codes and changes all characters to lower case.
 *
 *  @param name The name(or pattern) to be sanitized
 *  @return pointer to a static buffer holding the sanitized output. The return value must be copied so it won't get changed in the following calls.
 */
char* G_DB_SanitizeName(const char* name);

/**
 * Searches the database for one or both parameters if given
 * If name pattern is searched, the search is case insensitive and
 * doesn't care colorcodes.
 * Users that are buffered but not in db yet are not searched.
 *
 * @param name_pattern the part of the name to search
 *
 * @param level level from which to search
 *
 * @return qboolean true if results can be fetched, false otherwise
 */
qboolean G_DB_SearchDatabase(const char* name_pattern, int32_t level, const char* IP);
/**
 * Sets the internal ponter within the result set to the user that
 * is fecthed next.
 *
 * @param index to the first user to fetch from resultset. possible range is 1-G_DB_GetResultCount()
 *
 * @return qboolean true if results can be fetched, false otherwise
 */
qboolean G_DB_SetResultStart(uint32_t index);
/**
 * Returns the amount of results in the result set of the last
 * search.
 *
 * @return the number of available results
 */
qboolean G_DB_GetResultCount(void);
/**
 * Gets the user pointed internally at and iterates to the next user
 * in the result set. User returned as a result can be edited and then
 * saved back to the database.
 *
 * @param handle pointer to the g_shrubbot_user_handle_t type that will be filled with the user
 *
 * @return qboolean true if handle was set, false otherwise
 */
qboolean G_DB_GetResultUser(g_shrubbot_user_handle_t *handle);

/**
 * Check is the player whitelisted from IP bans
 *
 * @param guid of the player checked
 *
 * @return qboolean true if player is whitelisted, qfalse otherwise
 */
qboolean G_DB_IsWhiteListed(const char *guid);


void G_DB_PruneUsers(void);

#endif
