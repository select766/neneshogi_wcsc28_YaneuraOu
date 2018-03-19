#include "stdafx.h"
#include "../source/shogi.h"
#include "shogieval.h"

ShogiEval::ShogiEval()
{
	eval_queue = new ipqueue<dnn_eval_obj>(16, std::string("neneshogi_eval"), true);
	result_queue = new ipqueue<dnn_result_obj>(16, std::string("neneshogi_result"), true);
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
	fill_channel_range(buf, 0, 86, 0.0F);
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

int ShogiEval::get(py::array_t<float, py::array::c_style> dnn_input, py::array_t<uint16_t, py::array::c_style> move_and_index)
{
	// dnn_input: (86,9,9) array
	// move_and_index: (600, 2) array 2=(move, flattened_index)
	dnn_eval_obj *eval_obj;
	while (!(eval_obj = eval_queue->begin_read()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	get_board_array(dnn_input.mutable_data(), *eval_obj);
	int n_moves = get_move_and_index_array(move_and_index.mutable_data(), *eval_obj);

	eval_queue->end_read();
	return n_moves;
}

void ShogiEval::put(int n_moves, py::array_t<uint16_t, py::array::c_style> move_and_prob, int16_t static_value)
{
	dnn_result_obj *result_obj;
	while (!(result_obj = result_queue->begin_write()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	result_obj->static_value = static_value;
	const uint16_t *move_and_prob_buf = move_and_prob.data();
	memcpy(result_obj->move_probs, move_and_prob_buf, sizeof(result_obj->move_probs[0]) * n_moves);
	result_obj->n_moves = n_moves;

	result_queue->end_write();

}
