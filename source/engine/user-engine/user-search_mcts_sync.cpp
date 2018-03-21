#include "../../extra/all.h"
#include <thread>
#include "user-search_common.h"
#include "mcts.h"

#ifdef USER_ENGINE_MCTS_SYNC
static TreeConfig* tree_config;

// USI�g���R�}���h"user"�������Ă���Ƃ��̊֐����Ăяo�����B�����Ɏg���Ă��������B
void user_test(Position& pos_, istringstream& is)
{
	string token;
	is >> token;
	if (token == "demo")
	{
		// �ǖʂ̃X�R�A�E���O�m���v�Z�̃f��
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

// USI�ɒǉ��I�v�V������ݒ肵�����Ƃ��́A���̊֐����`���邱�ƁB
// USI::init()�̂Ȃ�����R�[���o�b�N�����B
void USI::extra_option(USI::OptionsMap & o)
{
}

// �N�����ɌĂяo�����B���Ԃ̂�����Ȃ��T���֌W�̏����������͂����ɏ������ƁB
void Search::init()
{
	init_dnn_queues();
	tree_config = new TreeConfig();
	tree_config->c_puct = 1.0;
	tree_config->play_temperature = 0.01;
	tree_config->virtual_loss = 1.0;
}

// isready�R�}���h�̉������ɌĂяo�����B���Ԃ̂����鏈���͂����ɏ������ƁB
void  Search::clear()
{
	show_error_if_dnn_queue_fail();
}


static TreeNode* generate_root(Position &pos)
{
	TreeSelectResult tsr;
	float score;
	vector<Move> move_list;
	vector<float> value_p;

	mcts_sync_eval(pos, tsr, score, move_list, value_p, eval_queue, result_queue);
	if (move_list.size() == 0)
	{
		// �l��
		return nullptr;
	}

	return new TreeNode(tree_config, nullptr, 0, move_list, score, value_p);

}

// �T���J�n���ɌĂяo�����B
// ���̊֐����ŏ��������I��点�Aslave�X���b�h���N������Thread::search()���Ăяo���B
// ���̂���slave�X���b�h���I�������A�x�X�g�Ȏw�����Ԃ����ƁB
void MainThread::think()
{
	Move bestMove = MOVE_RESIGN;
	int max_nodes = 100;
	TreeNode* tree_root = generate_root(rootPos);
	if (tree_root)
	{
		for (int i = 0; i < max_nodes; i++)
		{
			TreeSelectResult tsr;
			if (tree_root->select(tsr))
			{
				// �V�K�m�[�h�̍쐬
				float score;
				vector<Move> move_list;
				vector<float> value_p;
				mcts_sync_eval(rootPos, tsr, score, move_list, value_p, eval_queue, result_queue);

				if (move_list.size() == 0)
				{
					// �V�K�m�[�h�͋l�݂�����
					score = -10.0;
				}

				TreeNode* new_node = new TreeNode(tree_config, tsr.final_node, tsr.final_edge_index, move_list, score, value_p);
				tsr.final_node->children[tsr.final_edge_index] = new_node;
			}
			else
			{
				// �]���s�v�i�l�݁j�m�[�h�ɒB����
				// select()�������I��backup����
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

// �T���{�́B���񉻂��Ă���ꍇ�A������slave�̃G���g���[�|�C���g�B
// MainThread::search()��virtual�ɂȂ��Ă���think()���Ăяo�����̂ŁAMainThread::think()����
// ���̊֐����Ăяo�������Ƃ��́AThread::search()�Ƃ��邱�ƁB
void Thread::search()
{
}

#endif
