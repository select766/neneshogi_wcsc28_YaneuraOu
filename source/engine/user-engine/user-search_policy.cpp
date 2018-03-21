#include "../../extra/all.h"
#include <thread>
#include "user-search_common.h"

#ifdef USER_ENGINE_POLICY
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
	if (rootPos.is_mated())
	{
		sync_cout << "bestmove resign" << sync_endl;
		return;
	}
	//�L���[�Ɍ��ǖʂ�����
	ipqueue_item<dnn_eval_obj> *eval_objs;
	while (!(eval_objs = eval_queue->begin_write()))
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}

	dnn_write_eval_obj(&eval_objs->elements[0], rootPos);
	eval_objs->count = 1;
	eval_queue->end_write();

	// �]����҂�
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

// �T���{�́B���񉻂��Ă���ꍇ�A������slave�̃G���g���[�|�C���g�B
// MainThread::search()��virtual�ɂȂ��Ă���think()���Ăяo�����̂ŁAMainThread::think()����
// ���̊֐����Ăяo�������Ƃ��́AThread::search()�Ƃ��邱�ƁB
void Thread::search()
{
}

#endif
