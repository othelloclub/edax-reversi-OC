/**
 * @file move.c
 *
 * @brief Move & list of moves management.
 *
 * @date 1998 - 2025
 * @author Richard Delorme
 * @author Toshihiko Okuhara
 * @version 4.5
 */

#include "move.h"

#include "bit.h"
#include "board.h"
#include "hash.h"
#include "search.h"
#include "settings.h"
#include "stats.h"

#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

const Move MOVE_INIT = {NULL, 0, NOMOVE, -SCORE_INF, 0};
const Move MOVE_PASS = {NULL, 0, PASS, -SCORE_INF, 0};

const unsigned char SQUARE_VALUE[] = {
	// JCW's score:
	18,  4,  16, 12, 12, 16,  4, 18,
	 4,  2,   6,  8,  8,  6,  2,  4,
	16,  6,  14, 10, 10, 14,  6, 16,
	12,  8,  10,  0,  0, 10,  8, 12,
	12,  8,  10,  0,  0, 10,  8, 12,
	16,  6,  14, 10, 10, 14,  6, 16,
	 4,  2,   6,  8,  8,  6,  2,  4,
	18,  4,  16, 12, 12, 16,  4, 18
};

/**
 * @brief Get a symetric square coordinate
 *
 * @param x Square coordinate.
 * @param sym Symetry.
 * @return Symetric square coordinate.
 */
int symetry(int x, const int sym)
{
	if (A1 <= x && x <= H8) {
		if (sym & 1) {
			x = x ^ 7;
		}
		if (sym & 2) {
			x = x ^ 070;
		}
		if (sym & 4) {
			x = (x >> 3) + (x & 7) * 8;
		}
		assert(A1 <= x && x <= H8);
	}

	return x;
}


/**
 * @brief Print a move to string
 *
 * Print the move, using letter case to distinguish player's color,
 * to a string.
 * @param x Square coordinate to print.
 * @param player Player color.
 * @param s Output string.
 * @return the output string.
 */
char* move_to_string(const int x, const int player, char *s)
{
	if (x == NOMOVE) {
		s[0] = '-';
		s[1] = '-';
	} else if (x == PASS) {
		s[0] = 'p';
		s[1] = 'a';
	} else if (x >= A1 && x <= H8) {
		s[0] = x % 8 + 'a';
		s[1] = x / 8 + '1';
	} else {
		s[0] = '?';
		s[1] = '?';
	}

	if (player == BLACK) {
		s[0] = toupper(s[0]);
		s[1] = toupper(s[1]);
	}
	s[2] = '\0';

	return s;
}

/**
 * @brief Print out a move
 *
 * Print the move, using letter case to distinguish player's color,
 * to an output stream.
 * @param x      square coordinate to print.
 * @param player player color.
 * @param f      output stream.
 */
void move_print(const int x, const int player, FILE *f)
{
	char s[3];

	move_to_string(x, player, s);
	if (x == PASS) {
		s[1] += 's' - 'a';	// ps/PS instead of pa/PA
	}
	fputc(s[0], f);
	fputc(s[1], f);
}


#ifdef TUNE_EDAX
#include "solver.h"

static int w_hash = 1 << 15;
static int w_eval = 1 << 15;
static int w_mobility = 1 << 15;
static int w_corner_stability = 1 << 11;
static int w_edge_stability = 1 << 11;
static int w_potential_mobility = 1 << 5;
static int w_low_parity = 1 << 3;
static int w_mid_parity = 1 << 2;
static int w_high_parity = 1 << 1;

void tune_move_evaluate(Search *search, const char *filename, const char *w_name)
{
	int best_w;
	unsigned long long best_n_nodes;
	unsigned long long n_nodes, t;
	int i, *w;

	if (strcmp(w_name, "w_eval") == 0) w = &w_eval;
	else if (strcmp(w_name, "w_mobility") == 0) w = &w_mobility;
	else if (strcmp(w_name, "w_corner_stability") == 0) w = &w_corner_stability;
	else if (strcmp(w_name, "w_edge_stability") == 0) w = &w_edge_stability;
	else if (strcmp(w_name, "w_potential_mobility") == 0) w = &w_potential_mobility;
	else if (strcmp(w_name, "w_low_parity") == 0) w = &w_low_parity;
	else if (strcmp(w_name, "w_mid_parity") == 0) w = &w_mid_parity;
	else if (strcmp(w_name, "w_high_parity") == 0) w = &w_high_parity;
	else {
		warn("unknown parameter %s\n", w_name);
		return;
	}

	best_n_nodes = ULLONG_MAX;
	best_w = *w;

	for (i = -1; i <= 20; ++i) {
		*w = (i >= 0 ? 1 << i : 0);
		t = -time_clock();
		n_nodes = solver(search, filename);
		t += time_clock();
		printf("%s %d : nodes %llu : time %.3f\n", w_name, *w, n_nodes, 0.001 * t);
		if (n_nodes < best_n_nodes) {
			best_n_nodes = n_nodes;
			best_w = *w;
		}
	}
	printf("Best %s %d : %llu\n", w_name, best_w, best_n_nodes);
	*w = best_w;
}

#else
#include "move-ordering-weights.h"
#endif

/**
 * @brief Get moves from a position.
 *
 * @param movelist movelist.
 * @param board board.
 * @return move number.
 */
int movelist_get_moves(MoveList *movelist, const Board *board)
{
	Move *previous = movelist->move;
	Move *move;
	unsigned long long moves = board_get_moves(board);
	int x, n;

	n = 0;
	foreach_bit (x, moves) {
		// An edited (unreachable) position can have more than MAX_MOVE legal moves (35 found); reachable ones have at most 33.
		// Keep the first MAX_MOVE moves rather than writing past the list (MoveList lives on the stack of each search level).
		// 'continue', not 'break': the 32-bit foreach_bit is two nested loops (WASM), so a break would not end it.
		if (n >= MAX_MOVE) continue;
		move = previous + 1;
		previous->next = move;
		board_get_move_flip(board, x, move);
		// move->score = -SCORE_INF;	// -INT_MAX?
		previous = move;
		++n;
	}
	previous->next = NULL;
	movelist->n_moves = n;
	return n;
}

/**
 * @brief Print out a movelist
 *
 * Print the moves, using letter case to distinguish player's color,
 * to an output stream.
 * @param movelist a list of moves.
 * @param player   player color.
 * @param f        output stream.
 */
void movelist_print(const MoveList *movelist, const int player, FILE *f)
{
	Move *iter;

	for (iter = movelist->move[0].next; iter != NULL; iter = iter->next) {
		move_print(iter->x, player, f);
		fprintf(f, "[%d] ", iter->score);
	}
}

/**
 * @brief Return the next best move from the list.
 *
 * @param previous_best Last best move.
 * @return the following best move in the list.
 */
#if defined(__INTEL_COMPILER) && defined(__linux__)	// ICC linux emits bogus code in NWS_endgame if inlined
__attribute__((noinline))
#endif
#include "move-select-core.inc"

/**
 * @brief Return the next best move from the list.
 *
 * @param previous_best Last best move.
 * @return the following best move in the list.
 */


/**
 * @brief Evaluate a list of move in order to sort it with depth 0.
 * (called from NWS_endgame and movelist_evaluate)
 *
 * @param movelist List of moves to sort.
 * @param search Position to evaluate.
 * @param hashmove Position (maybe) stored in the hashtable.
 */
#include "move-ordering-core.inc"

/**
 * @brief Sort a move as best.
 *
 * Put the best move at the head of the list.
 *
 * @param movelist List of moves to sort.
 * @param move Best move to to set first.
 * @return A move, occasionally illegal, whose successor was the original successor of the new best move.
 */


/**
 * @brief Sort all moves based on move cost & hash_table storage.
 *
 * @param movelist List of moves to sort.
 * @param hash_data  Data from the hash table.
 */


/**
 * @brief Sort all moves.
 * @param movelist   List of moves to sort.
 */


/**
 * @brief Exclude a move.
 * @param movelist   List of moves to sort.
 * @param move       Move to exclude.
 */


/**
 * @brief Initialize a sequence of moves.
 *
 * @param line the move sequence.
 * @param player color of the first player of the sequence.
 */
void line_init(Line *line, const int player)
{
	line->n_moves = 0;
	line->color = player;
}

/**
 * @brief Add a move to the sequence.
 *
 * @param line the move sequence.
 * @param x move coordinate.
 */
void line_push(Line* line, const int x)
{
	int i;

	assert(line->n_moves < GAME_SIZE);

	for (i = 0; i < line->n_moves; ++i) {
		assert(line->move[i] != x || x == PASS);
	}
	line->move[line->n_moves++] = (char) x;
}

/**
 * @brief Remove the last move from a sequence.
 *
 * @param line the move sequence.
 */
void line_pop(Line* line)
{
	assert(line->n_moves > 0);
	--line->n_moves;
}

/**
 * @brief Copy part of a sequence to another sequence.
 *
 * @param dest the destination move sequence.
 * @param src the source move sequence.
 * @param from the point to copy from.
 */
void line_copy(Line *dest, const Line *src, const int from)
{
	int i;

	for (i = from; i < src->n_moves; ++i) { // todo: replace by memcpy ?
		dest->move[i] = src->move[i];
	}
	dest->n_moves = src->n_moves;
	dest->color = src->color;
}

/**
 * @brief Print a move sequence.
 *
 * @param line the move sequence.
 * @param width width of the line to print (in characters).
 * @param separator a string to print between moves.
 * @param f output stream.
 */
void line_print(const Line *line, int width, const char *separator, FILE *f)
{
	int i;
	int player = line->color;
	int w = 2 + (separator ? strlen(separator) : 0);
	int len = abs(width);

	for (i = 0; i < line->n_moves && len > w; ++i, len -= w) {
		player = !player;
		if (separator && i) fputs(separator, f);
		move_print(line->move[i], player, f);
	}
	for (len = MIN(len, width); len > w; len -= w) {
		fputc(' ', f); fputc(' ', f); if (separator) fputs(separator, f);
	}
}

/**
 * @brief Line to string.
 *
 * @param line the move sequence.
 * @param n number of moves to add.
 * @param separator a string to print between moves.
 * @param string output string receiving the line.
 */
char* line_to_string(const Line *line, int n, const char *separator, char *string)
{
	int i;
	int player = line->color;
	int w = 2 + (separator ? strlen(separator) : 0);

	for (i = 0; i < line->n_moves && i < n; ++i) {
		move_to_string(line->move[i], player, string + w * i);
		player = !player;
		if (separator) strcpy(string + w * i + 2, separator);
	}
	string[w * i] =  '\0';
	return string;
}

/**
 * A position and a move issued from it.
 */
typedef struct {
	Board board;
	int x;
} MBoard;

/**
 * Array of MBoard.
 */
typedef struct MoveArray {
	MBoard *item; /**< dynamic array */
	int n;       /**< number of items in the array */
	int size;    /**< capacity of the array */
} MoveArray;

/**
 * @brief array initialisation.
 * @param array Array of positions.
 */
static void movearray_init(MoveArray *array)
{
	array->item = NULL;
	array->n = array->size = 0;
}

/**
 * @brief array supression.
 * @param array Array of positions.
 */
static void movearray_delete(MoveArray *array)
{
	free(array->item);
}

/**
 * @brief Append a position.
 * @param array Array of positions.
 * @param b Position.
 * @param x Move.
 * @return true if a position is added to the array, false if it is already present.
 */
static bool movearray_append(MoveArray *array, const Board *b, const int x)
{
	int i;
	for (i = 0; i < array->n; ++i) {
		if (array->item[i].x == x && board_equal(b, &(array->item[i].board))) return false;
	}

	if (array->size < array->n + 1) {
		array->item = (MBoard*) realloc(array->item, (array->size = (array->size + 1)) * sizeof (MBoard));
		if (array->item == NULL) fatal_error("Cannot re-allocate board array.\n");
	}
	array->item[array->n].board = *b;
	array->item[array->n].x = x;
	++array->n;
	return true;
}

/**
 * @brief Initialisation of the hash table.
 * @param hash Hash table.
 * @param bitsize Hash table size (as log2(size)).
 */
void movehash_init(MoveHash *hash, int bitsize)
{
	int i;

	hash->size = (1 << bitsize);
	hash->mask = hash->size - 1;
	hash->array = (MoveArray*) malloc(hash->size * sizeof (MoveArray));
	if (hash->array == NULL) fatal_error("Cannot re-allocate board array.\n");
	for (i = 0; i < hash->size; ++i) movearray_init(hash->array + i);
}

/**
 * @brief Free the hash table.
 * @param hash Hash table.
 */
void movehash_delete(MoveHash *hash)
{
	int i;

	for (i = 0; i < hash->size; ++i) movearray_delete(hash->array + i);
	free(hash->array);
}

/**
 * @brief Append a position to the hash table.
 * @param hash Hash table.
 * @param b Position.
 * @param x Move.
 * @return true if a position is added to the hash table, false otherwsise.
 */
bool movehash_append(MoveHash *hash, const Board *b, const int x)
{
	Board u;
	int y;
	unsigned long long h;

	y = symetry(x, board_unique(b, &u));
	h = board_get_hash_code(&u);
	return movearray_append(hash->array + (h & hash->mask), &u, y);
}

