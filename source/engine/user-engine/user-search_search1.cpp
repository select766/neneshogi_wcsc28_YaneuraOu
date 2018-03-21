#include "../../extra/all.h"
#include <thread>
#include "user-search_common.h"


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
		dnn_write_eval_obj(eval_obj, rootPos);
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
