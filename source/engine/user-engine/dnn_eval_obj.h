#pragma once
#include "../../shogi.h"

#ifdef USER_ENGINE_POLICY
class dnn_table_index
{
public:

};
#endif

class dnn_move_index
{
public:
	uint16_t move;
	uint16_t index;
};

class dnn_move_prob
{
public:
	uint16_t move;
	uint16_t prob_scaled;
};

class dnn_eval_obj
{
public:
	dnn_table_index index;
	uint8_t board[SQ_NB];
	uint32_t hand[COLOR_NB];
	uint8_t side_to_move;
	bool in_check;
	uint16_t game_ply;
	uint16_t n_moves;
	dnn_move_index move_indices[MAX_MOVES];
};

class dnn_result_obj
{
public:
	dnn_table_index index;
	int16_t static_value;
	uint16_t n_moves;
	dnn_move_prob move_probs[MAX_MOVES];
};
