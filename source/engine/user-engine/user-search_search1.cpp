#include "../../extra/all.h"
#include <thread>
#include "user-search_common.h"


#ifdef USER_ENGINE_SEARCH1

// USI�g���R�}���h"user"�������Ă���Ƃ��̊֐����Ăяo�����B�����Ɏg���Ă��������B
void user_test(Position& pos_, istringstream& is)
{
	string token;
	is >> token;
}

// USI�ɒǉ��I�v�V������ݒ肵�����Ƃ��́A���̊֐����`���邱�ƁB
// USI::init()�̂Ȃ�����R�[���o�b�N�����B
void USI::extra_option(USI::OptionsMap & o)
{
}

// �N�����ɌĂяo�����B���Ԃ̂�����Ȃ��T���֌W�̏����������͂����ɏ������ƁB
void Search::init()
{
	init_dnn_queues();
}

// isready�R�}���h�̉������ɌĂяo�����B���Ԃ̂����鏈���͂����ɏ������ƁB
void  Search::clear()
{
	show_error_if_dnn_queue_fail();
}


// �T���J�n���ɌĂяo�����B
// ���̊֐����ŏ��������I��点�Aslave�X���b�h���N������Thread::search()���Ăяo���B
// ���̂���slave�X���b�h���I�������A�x�X�g�Ȏw�����Ԃ����ƁB
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

		//�L���[�Ɍ��ǖʂ�����
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
		// for�̒�����x�����s���ꂸ
		sync_cout << "bestmove resign" << sync_endl;
		return;
	}

	if (eval_objs)
	{
		// �o�b�`�T�C�Y�ɖ����Ȃ��������𑗂�
		eval_objs->count = write_index;
		eval_queue->end_write();
	}

	// �]����҂�
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
			Value score = (Value)(-result_obj->static_value);//���肩�猩���l�Ȃ̂Ŕ��]
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

// �T���{�́B���񉻂��Ă���ꍇ�A������slave�̃G���g���[�|�C���g�B
// MainThread::search()��virtual�ɂȂ��Ă���think()���Ăяo�����̂ŁAMainThread::think()����
// ���̊֐����Ăяo�������Ƃ��́AThread::search()�Ƃ��邱�ƁB
void Thread::search()
{
}
#endif
