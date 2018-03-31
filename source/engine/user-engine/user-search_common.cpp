#include "user-search_common.h"

#ifdef USER_ENGINE

ipqueue<dnn_eval_obj> *eval_queue;
ipqueue<dnn_result_obj> *result_queue;

void init_dnn_queues(const string prefix)
{
	eval_queue = new ipqueue<dnn_eval_obj>(0, 0, prefix + "_eval", false);
	result_queue = new ipqueue<dnn_result_obj>(0, 0, prefix + "_result", false);
}

void show_error_if_dnn_queue_fail()
{
	if (!eval_queue->ok)
	{
		sync_cout << "info string eval queue error!" << sync_endl;
	}
	if (!result_queue->ok)
	{
		sync_cout << "info string result queue error!" << sync_endl;
	}
}

int dnn_get_move_index(const Position & pos, Move m)
{
	/*
	AlphaZero�̘_�����Q�l�ɍ쐬
	9x9�͈ړ����B
	139�����̂����A64�����́u�N�C�[���v�̓���(8����*�ő�8�}�X)�B
	2�����͌j�n�̓����B66�����͑O�q��64+2�����Ɠ����͓����ŁA����ꍇ�B
	7�����͋��łꍇ�ŁA���̏ꍇ�̍��W�͑ł�B
	�N�C�[���̓�����(��,�i)=(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)
	* ���Ԃ̍ۂ́A�ՖʁE��̏����𔽓]���Đ��Ԃ̏�Ԃɂ���B
	*/
	Color side_to_move = pos.side_to_move();
	Square _move_to = move_to(m);
	if (side_to_move == WHITE) {
		_move_to = Inv(_move_to);
	}

	if (is_drop(m))
	{
		return (move_dropped_piece(m) - PAWN + 132) * (int)SQ_NB + _move_to;
	}
	else
	{
		Square _move_from = move_from(m);
		if (side_to_move == WHITE) {
			_move_from = Inv(_move_from);
		}

		int file_diff = file_of(_move_to) - file_of(_move_from);
		int rank_diff = rank_of(_move_to) - rank_of(_move_from);
		int ch;
		if (file_diff == -1 && rank_diff == -2)
		{
			ch = 64;
		}
		else if (file_diff == 1 && rank_diff == -2)
		{
			ch = 65;
		}
		else if (file_diff < 0)
		{
			if (rank_diff < 0)
			{
				ch = -1 + -file_diff;
			}
			else if (rank_diff == 0)
			{
				ch = 7 + -file_diff;
			}
			else
			{
				ch = 15 + -file_diff;
			}
		}
		else if (file_diff == 0)
		{
			if (rank_diff < 0)
			{
				ch = 23 + -rank_diff;
			}
			else
			{
				ch = 31 + rank_diff;
			}
		}
		else
		{
			// fild_diff > 0
			if (rank_diff < 0)
			{
				ch = 39 + file_diff;
			}
			else if (rank_diff == 0)
			{
				ch = 47 + file_diff;
			}
			else
			{
				ch = 55 + file_diff;
			}
		}

		if (is_promote(m))
		{
			ch += 66;
		}

		return ch * (int)SQ_NB + _move_from;
	}
}

bool dnn_write_eval_obj(dnn_eval_obj *eval_obj, const Position &pos)
{
	for (size_t i = 0; i < SQ_NB; i++)
	{
		eval_obj->board[i] = (uint8_t)pos.piece_on((Square)i);
	}
	for (Color i = COLOR_ZERO; i < COLOR_NB; i++)
	{
		eval_obj->hand[i] = pos.hand_of(i);
	}
	eval_obj->side_to_move = (uint8_t)pos.side_to_move();
	eval_obj->in_check = pos.in_check();
	eval_obj->game_ply = (uint16_t)pos.game_ply();

	int m_i = 0;
	bool not_mate = false;
	for (auto m : MoveList<LEGAL>(pos))
	{
		dnn_move_index dmi;
		dmi.move = (uint16_t)m.move;
		dmi.index = dnn_get_move_index(pos, m.move);
		eval_obj->move_indices[m_i] = dmi;
		m_i++;
		not_mate = true;
	}
	eval_obj->n_moves = m_i;
	return not_mate;
}

#endif
