/*
 *  Module contains aliases database functionality
 *
 *  Changelog ( date, changes, author ):
 *  2012-09-03, Initial version, gaoesa
 *  2012-09-06, Base work done with memory management (online !aliases works), gaoesa
*/

#ifndef __G_DB_ALIASES_H__
#define __G_DB_ALIASES_H__

// structure is used in the file as well as in the program
typedef struct db_alias_s {
	char name[36];
	char clean_name[36];
	int first_seen;
	int last_seen;
	int time_played;
} db_alias_t;

// search results
#define ALIASES_DB_MAXALIASES_FORONERESULT 10
typedef struct db_alias_searchresult_s {
	char		guid[33];
	uint32_t	totalPlayTime;
	uint32_t	numberOfAliases;
	qboolean	dontFit;
	db_alias_t	*aliases[ALIASES_DB_MAXALIASES_FORONERESULT];
} db_alias_searchresult_t;

/**
 *  Function initializes the alias database. This must be called from the main module after the file system is already set.
 *
 * @return 0 on success, -1 if file was created, -2 if the file exists but can't be used, -3 if aliases are not in use at all
 */
int G_DB_InitAliases(void);

/**
 *  Function closes the aliases database and clean up everything. This function is also responsible for maintaining the files.
 */
void G_DB_CloseAliases(void);

/**
 *  Function cleans up all aliases that don't have player records in the main db.
 */
void G_DB_CleanUpAliases(void);

/**
 *  Function removes aliases from that player from the database.
 *
 *  @param guid The 32 character silEnT GUID of the player.
 *  @param guidHash Optional guid hash. If set to 0, the guid hash is calculated from the guid.
 */
void G_DB_RemoveAliases(const char *guid, uint32_t guidHash);

/**
 *  Function removes aliases from that player from the database.
 *
 *  @param shortGuid The 8 character silEnT GUID of the player.
 *  @return qtrue if succesfull, qfalse if not
 */
qboolean G_DB_RemoveAliasesShortGUID(const char *shortGuid);

/**
 *  Function updates the alias into the database or creates a new record if an old one does not exist yet.
 *  This function does not do instant file writes, but nevertheless, the data is set so that it will be stored into the database files.
 *  This is the function that is used to add all aliases data, including the case when the player is encountered the first time.
 *
 *  @param guid The 32 character silEnT GUID of the player. This is treated as an array and not as a string. It is safe to not have terminating NUL.
 *  @param alias The fully set data that is updated to the database. If the alias is old, the old data is updated based on the data supplied with this.
 *  @param guidHash To optimize the function, the silEnT GUID hash can be given if available. If this parameter is 0, the hash will be calculated in the function.
 */
void G_DB_UpdateAlias(const char *guid, const db_alias_t *alias, uint32_t guidHash);

/**
 *  Function returns the number of aliases and sets the internal iterator to the first alias if available.
 *
 *  @param guid Is the 32 character silEnT GUID of the player.
 *  @param start The first record that is returned from G_DB_GetNextAlias
 *  @return the number of aliases to read with G_DB_GetNextAlias function calls or, -1 if aliases not in use, -2 if bad guid or -3 if not found
*/
int G_DB_GetAliases(const char *guid, const int start);

/**
 *  Function searches the alias database for the player using the short guid. After this function the players can
 *  be sequentially read using the G_DB_GetNextAlias function.
 *
 *  @param guid The 8 character short silEnT GUID of the player to search.
 *  @param start The first record that will bepointed to with internal pointer and is retrieved using G_DB_GetNextAlias
 *  @return The total number of aliases of the found player. Or -1 if aliases not in use.
 */
int G_DB_SearchAliasesShortGUID(const char *guid, const int start);

/**
 *  Function is used to search all users that have used the name.
 *
 *  @param pattern The searched pattern. Must not have color codes in it.
 *  @return number of found records or -1 if aliases not in use or -2 if all the found ones can't fit into the results.
 */
int G_DB_SearchAliasesNamePattern(const char *pattern);

/**
 *	Function returns the found data of the player.
 *
 *  @param position The index in the search results. Between 0 - G_DB_SearchAliasesNamePattern
 *  @return The found data of the player or NULL
 */
db_alias_searchresult_t *G_DB_GetAliasesSearchResult(int position);

/**
 *  Function return an alias pointed by internal iterator. Before this function is called, the iterator must be set with G_DB_GetAliases.
 *  After the function call the iterator is advanced to the next alias until all the aliases are iterated. If all the aliases are
 *  already iterated or the G_DB_GetAliases failed, NULL is returned.
 *
 *  @return Pointer to an alias in the aliases module memory. The data pointed by the pointer must not be changed.
 */
const db_alias_t* G_DB_GetNextAlias(void);

#endif
