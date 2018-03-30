#include "stdafx.h"
#include "shogieval.h"

ShogiEval::ShogiEval(int size, int batch_size)
{
	eval_queue = new ipqueue<dnn_eval_obj>(size, batch_size, std::string("neneshogi_eval"), true);
	result_queue = new ipqueue<dnn_result_obj>(size, batch_size, std::string("neneshogi_result"), true);
}


static void fill_channel(float* buf, int ch, float value)
{
	for (Square i = SQ_ZERO; i < SQ_NB; i++) {
		buf[ch * SQ_NB + i] = value;
	}
}

static void fill_channel_range(float* buf, int ch_begin, int ch_end, float value)
{
	while (ch_begin < ch_end)
	{
		fill_channel(buf, ch_begin++, value);
	}
}

static void get_board_array(float* buf, dnn_eval_obj& eval_obj)
{
	fill_channel_range(buf, 0, ShogiEval::DNN_INPUT_CHANNEL, 0.0F);
	if (eval_obj.side_to_move == BLACK) {
		for (Square i = SQ_ZERO; i < SQ_NB; i++) {
			Piece p = (Piece)eval_obj.board[i];
			if (p == PIECE_ZERO) {
				continue;
			}
			int ch;
			if (color_of(p) == BLACK) {
				ch = p - B_PAWN;
			}
			else {
				ch = p - W_PAWN + 14;
			}
			buf[ch * SQ_NB + i] = 1;
		}
	}
	else {
		for (Square i = SQ_ZERO; i < SQ_NB; i++) {
			Piece p = (Piece)eval_obj.board[i];
			if (p == PIECE_ZERO) {
				continue;
			}
			int ch;
			// æŽèŒãŽè“ü‚ê‘Ö‚¦+À•W‰ñ“]
			if (color_of(p) == BLACK) {
				ch = p - B_PAWN + 14;
			}
			else {
				ch = p - W_PAWN;
			}
			buf[ch * SQ_NB + Inv(i)] = 1;
		}

	}

	int ch_ofs = 28;
	for (int i = 0; i < 2; i++) {
		Hand hand = (Hand)eval_obj.hand[i ^ eval_obj.side_to_move];
		//•à‚ÍÅ‘å8–‡
		fill_channel_range(buf, ch_ofs, (std::min)(ch_ofs + hand_count(hand, PAWN), 8), 1.0);
		ch_ofs += 8;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, LANCE), 1.0);
		ch_ofs += 4;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, KNIGHT), 1.0);
		ch_ofs += 4;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, SILVER), 1.0);
		ch_ofs += 4;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, BISHOP), 1.0);
		ch_ofs += 2;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, ROOK), 1.0);
		ch_ofs += 2;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, GOLD), 1.0);
		ch_ofs += 4;
	}

	fill_channel(buf, 84, eval_obj.game_ply / 256.0F);
	fill_channel(buf, 85, (float)eval_obj.in_check);
}

static int get_move_and_index_array(uint16_t *buf, dnn_eval_obj &eval_obj)
{
	for (size_t i = 0; i < eval_obj.n_moves; i++)
	{
		buf[i * 2] = eval_obj.move_indices[i].move;
		buf[i * 2 + 1] = eval_obj.move_indices[i].index;
	}
	return eval_obj.n_moves;
}

py::tuple ShogiEval::get(py::array_t<float, py::array::c_style> dnn_input, py::array_t<uint16_t, py::array::c_style> move_and_index,
	py::array_t<uint16_t, py::array::c_style> n_moves)
{
	// dnn_input: (n,86,9,9) array
	// move_and_index: (n,600,2) array 2=(move, flattened_index)
	// n_moves: (n,) array
	// returns elements count and dnn_table_index
	ipqueue_item<dnn_eval_obj> *eval_objs;
	while (!(eval_objs = eval_queue->begin_read()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	float *dnn_input_data = dnn_input.mutable_data();
	uint16_t *move_and_index_data = move_and_index.mutable_data();
	uint16_t *n_moves_data = n_moves.mutable_data();
	dnn_table_index *dnn_table_indexes = new dnn_table_index[eval_objs->count];
	for (size_t i = 0; i < eval_objs->count; i++)
	{
		get_board_array(dnn_input_data, eval_objs->elements[i]);
		*n_moves_data = get_move_and_index_array(move_and_index_data, eval_objs->elements[i]);
		dnn_table_indexes[i] = eval_objs->elements[i].index;
		dnn_input_data += ShogiEval::DNN_INPUT_CHANNEL * 9 * 9;
		move_and_index_data += MOVE_SIZE * 2;
		n_moves_data++;
	}


	eval_queue->end_read();
	
	auto t = py::make_tuple((int)eval_objs->count, py::bytes(reinterpret_cast<char*>(dnn_table_indexes), sizeof(dnn_table_index)*eval_objs->count));
	delete[] dnn_table_indexes;
	return t;
}

void ShogiEval::put(int count, std::string dnn_table_indexes, py::array_t<uint16_t, py::array::c_style> move_and_prob,
	py::array_t<uint16_t, py::array::c_style> n_moves, py::array_t<int16_t, py::array::c_style> static_value)
{
	ipqueue_item<dnn_result_obj> *result_objs;
	while (!(result_objs = result_queue->begin_write()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	result_objs->count = count;
	const dnn_table_index *dnn_table_indexes_casted = reinterpret_cast<const dnn_table_index*>(dnn_table_indexes.c_str());
	const int16_t *static_value_data = static_value.data();
	const uint16_t *move_and_prob_data = move_and_prob.data();
	const uint16_t *n_moves_data = n_moves.data();
	for (size_t i = 0; i < count; i++)
	{
		memcpy(result_objs->elements[i].move_probs, move_and_prob_data, sizeof(uint16_t) * MOVE_SIZE * 2);
		result_objs->elements[i].index = dnn_table_indexes_casted[i];
		result_objs->elements[i].n_moves = *n_moves_data;
		result_objs->elements[i].static_value = *static_value_data;
		move_and_prob_data += MOVE_SIZE * 2;
		n_moves_data++;
		static_value_data++;
	}

	result_queue->end_write();

}
