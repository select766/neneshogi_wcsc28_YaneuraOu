#include "../../extra/all.h"
#include <thread>
#include "user-search_common.h"

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
	init_dnn_queues();
}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void  Search::clear()
{
	show_error_if_dnn_queue_fail();
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

	dnn_write_eval_obj(&eval_objs->elements[0], rootPos);
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
