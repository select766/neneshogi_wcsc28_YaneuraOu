#include "../../extra/all.h"
#include <thread>
#include "mcts.h"
#include "../../../ipqueue/ipqueue/ipqueue.h"
#include "dnn_eval_obj.h"

#ifndef USER_ENGINE
void user_test(Position& pos_, istringstream& is)
{
	// dummy
}
#endif

#ifdef USER_ENGINE

ipqueue<dnn_eval_obj> *eval_queue;
ipqueue<dnn_result_obj> *result_queue;

static int get_move_index(const Position & pos, Move m)
{
	/*
	AlphaZeroの論文を参考に作成
	9x9は移動元。
	139次元のうち、64次元は「クイーン」の動き(8方向*最大8マス)。
	2次元は桂馬の動き。66次元は前述の64+2次元と動きは同じで、成る場合。
	7次元は駒を打つ場合で、この場合の座標は打つ先。
	クイーンの動きは(筋,段)=(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)
	* 後手番の際は、盤面・駒の所属を反転して先手番の状態にする。
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

static void write_eval_obj(dnn_eval_obj *eval_obj, const Position &pos)
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
	for (auto m : MoveList<LEGAL>(pos))
	{
		dnn_move_index dmi;
		dmi.move = (uint16_t)m.move;
		dmi.index = get_move_index(pos, m.move);
		eval_obj->move_indices[m_i] = dmi;
		m_i++;
	}
	eval_obj->n_moves = m_i;
}

#ifdef USER_ENGINE_POLICY
// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos_, istringstream& is)
{
	string token;
	is >> token;
}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap & o)
{
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init()
{
	eval_queue = new ipqueue<dnn_eval_obj>(0, 0, std::string("neneshogi_eval"), false);
	result_queue = new ipqueue<dnn_result_obj>(0, 0, std::string("neneshogi_result"), false);
}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void  Search::clear()
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


// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::think()
{
	if (rootPos.is_mated())
	{
		sync_cout << "bestmove resign" << sync_endl;
		return;
	}
	//キューに現局面を入れる
	ipqueue_item<dnn_eval_obj> *eval_objs;
	while (!(eval_objs = eval_queue->begin_write()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	write_eval_obj(&eval_objs->elements[0], rootPos);
	eval_objs->count = 1;
	eval_queue->end_write();

	// 評価を待つ
	ipqueue_item<dnn_result_obj> *result_objs;
	while (!(result_objs = result_queue->begin_read()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
	dnn_result_obj *result_obj = &result_objs->elements[0];

	sync_cout << "info score cp " << result_obj->static_value << sync_endl;
	dnn_move_prob best_move_prob;
	best_move_prob.prob_scaled = 0;
	for (size_t i = 0; i < result_obj->n_moves; i++)
	{
		dnn_move_prob &mp = result_obj->move_probs[i];
		sync_cout << "info string move " << (Move)mp.move << " prob " << mp.prob_scaled << sync_endl;
		if (mp.prob_scaled > best_move_prob.prob_scaled)
		{
			best_move_prob = mp;
		}
	}

	result_queue->end_read();

	Move bestMove = (Move)best_move_prob.move;
	sync_cout << "bestmove " << bestMove << sync_endl;
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
// MainThread::search()はvirtualになっていてthink()が呼び出されるので、MainThread::think()から
// この関数を呼び出したいときは、Thread::search()とすること。
void Thread::search()
{
}

#endif

#ifdef USER_ENGINE_SEARCH1

// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos_, istringstream& is)
{
	string token;
	is >> token;
}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap & o)
{
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init()
{
	eval_queue = new ipqueue<dnn_eval_obj>(0, 0, std::string("neneshogi_eval"), false);
	result_queue = new ipqueue<dnn_result_obj>(0, 0, std::string("neneshogi_result"), false);
}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void  Search::clear()
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


// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::think()
{
	ipqueue_item<dnn_eval_obj> *eval_objs = nullptr;
	int write_index = 0;
	int total_moves = 0;
	for (auto m : MoveList<LEGAL>(rootPos))
	{
		StateInfo si;
		rootPos.do_move(m, si);
		if (!eval_objs)
		{
			while (!(eval_objs = eval_queue->begin_write()))
			{
				std::this_thread::sleep_for(std::chrono::microseconds(1));
			}
			write_index = 0;
		}

		//キューに現局面を入れる
		dnn_eval_obj *eval_obj = &eval_objs->elements[write_index++];
		write_eval_obj(eval_obj, rootPos);
		eval_obj->index.m = m;
		total_moves++;

		if (write_index == eval_queue->batch_size())
		{
			eval_objs->count = write_index;
			eval_queue->end_write();
			eval_objs = nullptr;
		}

		rootPos.undo_move(m);
	}

	if (total_moves == 0)
	{
		// forの中が一度も実行されず
		sync_cout << "bestmove resign" << sync_endl;
		return;
	}

	if (eval_objs)
	{
		// バッチサイズに満たなかった分を送る
		eval_objs->count = write_index;
		eval_queue->end_write();
	}

	// 評価を待つ
	int receive_count = 0;
	ipqueue_item<dnn_result_obj> *result_objs = nullptr;
	Move bestMove = MOVE_RESIGN;
	Value bestScore = -VALUE_INFINITE;
	while (receive_count < total_moves)
	{
		while (!(result_objs = result_queue->begin_read()))
		{
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}

		for (size_t i = 0; i < result_objs->count; i++)
		{
			dnn_result_obj *result_obj = &result_objs->elements[i];
			Value score = (Value)(-result_obj->static_value);//相手から見た値なので反転
			sync_cout << "info string move " << result_obj->index.m << " score " << score << sync_endl;
			if (score > bestScore)
			{
				bestScore = score;
				bestMove = result_obj->index.m;
			}

			receive_count++;
		}

		result_queue->end_read();
	}

	sync_cout << "info score cp " << bestScore / 100 << " pv " << bestMove << sync_endl;
	sync_cout << "bestmove " << bestMove << sync_endl;
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
// MainThread::search()はvirtualになっていてthink()が呼び出されるので、MainThread::think()から
// この関数を呼び出したいときは、Thread::search()とすること。
void Thread::search()
{
}
#endif

#ifdef USER_ENGINE_MCTS
static TreeConfig* tree_config;

// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos_, istringstream& is)
{
	string token;
	is >> token;
	if (token == "demo")
	{
		// 局面のスコア・事前確率計算のデモ
		TreeSelectResult tsr;
		for (size_t i = 0; i < 2; i++)
		{
			float score;
			vector<Move> move_list;
			vector<float> value_p;

			if (i == 1)
			{
				tsr.moves.push_back(move_from_usi("7g7f"));
			}

			pseudo_eval(pos_, tsr, score, move_list, value_p);
			sync_cout << "score " << score << sync_endl;
			for (size_t i = 0; i < move_list.size(); i++)
			{
				sync_cout << "move " << move_list[i] << " prob " << value_p[i] << sync_endl;
			}

		}

	}
	else if (token == "eval")
	{
		TreeSelectResult tsr;
		float score;
		vector<Move> move_list;
		vector<float> value_p;

		pseudo_eval(pos_, tsr, score, move_list, value_p);
		sync_cout << "{";
		cout << "\"\":" << score;
		for (size_t i = 0; i < move_list.size(); i++)
		{
			cout << ",\"" << move_list[i] << "\":" << value_p[i];
		}
		cout << "}" << sync_endl;
	}
	else if (token == "sfen")
	{
		sync_cout << pos_.sfen() << sync_endl;
	}
}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap & o)
{
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init()
{
	tree_config = new TreeConfig();
	tree_config->c_puct = 1.0;
	tree_config->play_temperature = 0.01;
	tree_config->virtual_loss = 1.0;
}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void  Search::clear()
{
}


static TreeNode* generate_root(Position &pos)
{
	TreeSelectResult tsr;
	float score;
	vector<Move> move_list;
	vector<float> value_p;

	pseudo_eval(pos, tsr, score, move_list, value_p);
	if (move_list.size() == 0)
	{
		// 詰み
		return nullptr;
	}

	return new TreeNode(tree_config, nullptr, 0, move_list, score, value_p);

}

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::think()
{
	Move bestMove = MOVE_RESIGN;
	int max_nodes = 10000;
	TreeNode* tree_root = generate_root(rootPos);
	if (tree_root)
	{
		for (int i = 0; i < max_nodes; i++)
		{
			TreeSelectResult tsr;
			if (tree_root->select(tsr))
			{
				// 新規ノードの作成
				float score;
				vector<Move> move_list;
				vector<float> value_p;
				pseudo_eval(rootPos, tsr, score, move_list, value_p);

				if (move_list.size() == 0)
				{
					// 新規ノードは詰みだった
					score = -10.0;
				}

				TreeNode* new_node = new TreeNode(tree_config, tsr.final_node, tsr.final_edge_index, move_list, score, value_p);
				tsr.final_node->children[tsr.final_edge_index] = new_node;
			}
			else
			{
				// 評価不要（詰み）ノードに達した
				// select()が内部的にbackupする
			}
		}

		float bestScore;
		tree_root->play(&bestMove, &bestScore);
		vector<Move> pv;
		float pvScore;
		tree_root->get_pv(pv, pvScore, bestMove);
		sync_cout << "info score cp " << (int)(pvScore * 1000) << " pv";
		for (auto m : pv)
		{
			std::cout << " " << m;
		}
		std::cout << sync_endl;
		vector<int> depth_stat;
		tree_root->depth_stat(depth_stat);
		sync_cout << "info string d=";
		for (int i = 0; i < 20; i++)
		{
			std::cout << depth_stat[i] << ",";
		}
		std::cout << sync_endl;
		int sum_n = 0;
		for (auto vn : tree_root->value_n)
		{
			sum_n += vn;
		}
		sync_cout << "info string vn: " << sum_n << sync_endl;

		TreeNode::free(tree_root);
	}

	sync_cout << "bestmove " << bestMove << sync_endl;
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
// MainThread::search()はvirtualになっていてthink()が呼び出されるので、MainThread::think()から
// この関数を呼び出したいときは、Thread::search()とすること。
void Thread::search()
{
}

#endif

#endif // USER_ENGINE
