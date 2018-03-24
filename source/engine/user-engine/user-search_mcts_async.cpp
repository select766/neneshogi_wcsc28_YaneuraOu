#include "../../extra/all.h"
#include <thread>
#include "user-search_common.h"
#include "mcts.h"

#ifdef USER_ENGINE_MCTS_ASYNC
namespace MCTSAsync
{
	class NodeHashEntry
	{
	public:
		Key key;
		int game_ply;
		bool flag;
	};


	class DupEvalChain
	{
	public:
		dnn_table_index path;
		DupEvalChain *next;
	};

	class UctNode
	{
	public:
		int value_n_sum;
		bool terminal;
		bool evaled;
		DupEvalChain *dup_eval_chain;//������]�����Ă΂ꂽ�Ƃ��A�����Ƀ��X�g���Ȃ��Ċe�o�H��backup����B
		float score;
		int n_children;
		Move move_list[MAX_MOVES];
		int value_n[MAX_MOVES];
		float value_w[MAX_MOVES];
		float value_p[MAX_MOVES];
		float value_q[MAX_MOVES];
		int vloss_ctr[MAX_MOVES];//virtual loss�������ƕ��A���ꂽ���m�F�p
	};

	class NodeHash
	{
	public:
		int uct_hash_size;
		int uct_hash_limit;
		unsigned int uct_hash_mask;
		int used;
		bool enough_size;
		NodeHashEntry *entries;
		UctNode *nodes;

		NodeHash(int uct_hash_size) :uct_hash_size(uct_hash_size), used(0), enough_size(true)
		{
			uct_hash_limit = (int)(uct_hash_size * 0.9);
			uct_hash_mask = (unsigned int)uct_hash_size - 1;
			entries = new NodeHashEntry[uct_hash_size]();
			nodes = new UctNode[uct_hash_size]();
		}

		int find_or_create_index(const Position &pos, bool *created)
		{
			Key key = pos.key();
			int orig_index = (unsigned int)key & uct_hash_mask;
			int index = orig_index;
			while (true)
			{
				NodeHashEntry *nhe = &entries[index];
				if (nhe->flag)
				{
					if (nhe->key == key && nhe->game_ply == pos.game_ply())
					{
						*created = false;
						return index;
					}

				}
				else
				{
					nhe->key = key;
					nhe->game_ply = pos.game_ply();
					nhe->flag = true;
					used++;
					if (used > uct_hash_limit)
					{
						enough_size = false;
					}
					*created = true;
					return index;
				}
				index = (index + 1) & uct_hash_mask;
				if (index == orig_index)
				{
					// full
					return -1;
				}
			}
		}

		int find_index(const Position &pos)
		{
			Key key = pos.key();
			int orig_index = (unsigned int)key & uct_hash_mask;
			int index = orig_index;
			while (true)
			{
				NodeHashEntry *nhe = &entries[index];
				if (nhe->flag)
				{
					if (nhe->key == key && nhe->game_ply == pos.game_ply())
					{
						return index;
					}

				}
				else
				{
					return -1;
				}
				index = (index + 1) & uct_hash_mask;
				if (index == orig_index)
				{
					// full
					return -1;
				}
			}
		}


	};

}

using namespace MCTSAsync;

static NodeHash *node_hash = nullptr;
static int eval_queue_batch_index = 0;
static ipqueue_item<dnn_eval_obj> *eval_objs = nullptr;
static int n_batch_put = 0;
static int n_batch_get = 0;
static TreeConfig tree_config;

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
	node_hash = new NodeHash(128 * 1024);
	tree_config.c_puct = 10.0;
	tree_config.play_temperature = 0.1;
	tree_config.virtual_loss = 1.0;
#ifdef _DEBUG
	std::this_thread::sleep_for(std::chrono::seconds(5));
#endif
}

void flush_queue()
{
	if (eval_queue_batch_index > 0)
	{
		eval_objs->count = eval_queue_batch_index;
		eval_queue->end_write();
		eval_objs = nullptr;
		eval_queue_batch_index = 0;
		n_batch_put++;
	}
}

bool enqueue_pos(const Position &pos, dnn_table_index &path)
{
	if (!eval_objs)
	{
		while (!(eval_objs = eval_queue->begin_write()))
		{
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
		eval_queue_batch_index = 0;
	}
	dnn_eval_obj *eval_obj = &eval_objs->elements[eval_queue_batch_index];
	bool not_mate = dnn_write_eval_obj(eval_obj, pos);
	if (not_mate)
	{
		eval_obj->index = path;
		eval_queue_batch_index++;
		if (eval_queue_batch_index == eval_queue->batch_size())
		{
			flush_queue();
		}
	}
	return not_mate;
}


void backup_tree(float leaf_score, dnn_table_index &path)
{
	float score = leaf_score;

	// tree�����ǂ�l���X�V
	for (int i = path.path_length - 2; i >= 0; i--)
	{
		score = -score;
		UctNode &inner_node = node_hash->nodes[path.path_indices[i]];
		uint16_t edge = path.path_child_indices[i];
		float new_value_n = inner_node.value_n[edge] + 1.0F - tree_config.virtual_loss;
		inner_node.value_n[edge] = new_value_n;
		float new_value_w = inner_node.value_w[edge] + score + tree_config.virtual_loss;
		inner_node.value_w[edge] = new_value_w;
		inner_node.vloss_ctr[edge]--;
		inner_node.value_q[edge] = new_value_w / new_value_n;
		inner_node.value_n_sum += 1.0F - tree_config.virtual_loss;
	}
}

void update_on_dnn_result(dnn_result_obj *result_obj)
{
	dnn_table_index &path = result_obj->index;
	// ���[�m�[�h�̕]�����L�^
	UctNode &leaf_node = node_hash->nodes[path.path_indices[path.path_length - 1]];
	leaf_node.evaled = true;
	for (int i = 0; i < result_obj->n_moves; i++)
	{
		dnn_move_prob &dmp = result_obj->move_probs[i];
		leaf_node.move_list[i] = (Move)dmp.move;
		leaf_node.value_p[i] = dmp.prob_scaled / 65535.0F;
		// n, w, q��0����������Ă���
	}
	leaf_node.n_children = result_obj->n_moves;
	float score = result_obj->static_value / 32000.0F;
	leaf_node.score = score;

	backup_tree(score, path);
	DupEvalChain *dec = leaf_node.dup_eval_chain;
	while (dec != nullptr)
	{
		backup_tree(score, dec->path);
		DupEvalChain *dec_next = dec->next;
		delete dec;
		dec = dec_next;
	}
}

void update_on_mate(dnn_table_index &path)
{
	// �V�K�W�J�m�[�h��mate�������Ƃ��̏���
	UctNode &leaf_node = node_hash->nodes[path.path_indices[path.path_length - 1]];
	leaf_node.evaled = true;
	leaf_node.terminal = true;
	float score = -1.0F;
	sync_cout << "info string update_on_mate" << sync_endl;
	backup_tree(score, path);
}

void update_on_terminal(dnn_table_index &path)
{
	// ���B�m�[�h��terminal�������Ƃ��̏���
	UctNode &leaf_node = node_hash->nodes[path.path_indices[path.path_length - 1]];
	sync_cout << "info string update_on_terminal" << sync_endl;
	backup_tree(leaf_node.score, path);
}

bool receive_result(bool block)
{
	int receive_count = 0;
	ipqueue_item<dnn_result_obj> *result_objs = nullptr;
	if (block)
	{
		while (!(result_objs = result_queue->begin_read()))
		{
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}
	else
	{
		result_objs = result_queue->begin_read();
		if (!result_objs)
		{
			return false;
		}
	}

	for (size_t i = 0; i < result_objs->count; i++)
	{
		update_on_dnn_result(&result_objs->elements[i]);
		receive_count++;
	}

	result_queue->end_read();
	n_batch_get++;

	return true;
}

int get_or_create_root(const Position &pos)
{
	bool created;

	int index = node_hash->find_or_create_index(pos, &created);
	if (!created)
	{
		sync_cout << "info string root cached" << sync_endl;
		return index;
	}

	UctNode *node = &node_hash->nodes[index];

	// �ǖʕ]��
	dnn_table_index path;
	path.path_length = 1;
	path.path_indices[0] = index;
	bool not_mate = enqueue_pos(pos, path);
	if (not_mate)
	{
		// �]���҂�
		flush_queue();
		receive_result(true);
	}
	else
	{
		// �l��ł��ĕ]���ΏۂɂȂ�Ȃ�
		update_on_mate(path);
	}

	return index;
}

int select_edge(UctNode &node)
{
	float n_sum_sqrt = sqrt((float)node.value_n_sum) + 0.001F;
	int best_index = 0;
	float best_value = -100.0F;
	for (size_t i = 0; i < node.n_children; i++)
	{
		float value_u = node.value_p[i] / (node.value_n[i] + 1) * tree_config.c_puct * n_sum_sqrt;
		float value_sum = node.value_q[i] + value_u;
		if (value_sum > best_value)
		{
			best_value = value_sum;
			best_index = i;
		}
	}

	return best_index;
}

int total_dup_eval = 0;
bool dup_eval_flag = false;

void mcts_select(int node_index, dnn_table_index &path, Position &pos)
{
	UctNode &node = node_hash->nodes[node_index];
	if (node.terminal)
	{
		// �l�݃m�[�h
		// �]���͕s�v�ŁA�e�֕]���l���ēx�`�d����
		update_on_terminal(path);
		return;
	}

	if (!node.evaled)
	{
		// �m�[�h���]�����������ꍇ
		// virtual loss������̂ŁA�]�����I������Ƃ��ɒǉ���backup���ĂԂ悤�ɂ���
		// link list�ɂȂ�
		DupEvalChain *dec = new DupEvalChain();
		memcpy(&dec->path, &path, sizeof(dnn_table_index));
		dec->next = node.dup_eval_chain;
		node.dup_eval_chain = dec;
		total_dup_eval++;
		dup_eval_flag = true;
		return;
	}

	// �G�b�W�I��
	int edge = select_edge(node);

	// virtual loss���Z
	node.value_n[edge] += tree_config.virtual_loss;
	node.value_n_sum += tree_config.virtual_loss;
	node.value_w[edge] -= tree_config.virtual_loss;
	node.vloss_ctr[edge]++;
	node.value_q[edge] = node.value_w[edge] / node.value_n[edge];

	Move m = node.move_list[edge];
	StateInfo si;
	pos.do_move(m, si);

	// �q�m�[�h��I�����邩����
	bool created;
	int child_index = node_hash->find_or_create_index(pos, &created);
	path.path_child_indices[path.path_length - 1] = edge;
	path.path_indices[path.path_length] = child_index;
	path.path_length++;

	if (created)
	{
		// �V�K�q�m�[�h�Ȃ̂ŁA�]��
		bool not_mate = enqueue_pos(pos, path);
		if (not_mate)
		{
			// �]���҂�
			// �񓯊��ɏ��������
		}
		else
		{
			// �l��ł��ĕ]���ΏۂɂȂ�Ȃ�
			update_on_mate(path);
		}
	}
	else
	{
		// �ċA�I�ɒT��
		mcts_select(child_index, path, pos);
	}

	pos.undo_move(m);
}

float get_pv(int cur_index, vector<Move> &pv, Position &pos)
{
	UctNode *node = &node_hash->nodes[cur_index];
	if (node->terminal)
	{
		return 0.0F;
	}
	float best_n = -10.0;
	float winrate = 0.0;
	Move bestMove = MOVE_RESIGN;
	for (size_t i = 0; i < node->n_children; i++)
	{
		if (node->value_n[i] > best_n)
		{
			best_n =  node->value_n[i];
			bestMove = node->move_list[i];
			winrate = node->value_w[i] / node->value_n[i];
		}
	}
	if (pos.pseudo_legal(bestMove) && pos.legal(bestMove))
	{
		pv.push_back(bestMove);
		StateInfo si;
		pos.do_move(bestMove, si);
		int child_index = node_hash->find_index(pos);
		if (child_index >= 0)
		{
			get_pv(child_index, pv, pos);
		}
		pos.undo_move(bestMove);
	}
	return winrate;
}

void print_pv(int root_index, Position &rootPos)
{
	vector<Move> pv;
	float winrate = get_pv(root_index, pv, rootPos);
	sync_cout << "info score cp " << (int)(winrate * 10000.0) << " pv";
	for (auto m : pv)
	{
		std::cout << " " << m;
	}
	std::cout << sync_endl;
}

// �T���J�n���ɌĂяo�����B
// ���̊֐����ŏ��������I��点�Aslave�X���b�h���N������Thread::search()���Ăяo���B
// ���̂���slave�X���b�h���I�������A�x�X�g�Ȏw�����Ԃ����ƁB
void MainThread::think()
{
	int root_index = get_or_create_root(rootPos);
	UctNode &root_node = node_hash->nodes[root_index];
	Move bestMove = MOVE_RESIGN;
	int max_select = 1000;
	int n_select = 0;
	if (!root_node.terminal)
	{
		float best_p = -10.0;
		Move best_p_move = MOVE_RESIGN;
		for (size_t i = 0; i < root_node.n_children; i++)
		{
			if (root_node.value_p[i] > best_p)
			{
				best_p = root_node.value_p[i];
				best_p_move = root_node.move_list[i];
			}
		}
		sync_cout << "info score cp " << (int)(root_node.score * 10000) << " pv " << best_p_move << sync_endl;
		total_dup_eval = 0;
		while (n_select < max_select || n_batch_get < n_batch_put)
		{
			if (n_select < max_select)
			{
				dnn_table_index path;
				path.path_length = 1;
				path.path_indices[0] = root_index;
				dup_eval_flag = false;
				mcts_select(root_index, path, rootPos);
				n_select++;
				if (dup_eval_flag)
				{
					flush_queue();
				}
				if (n_select == max_select)
				{
					flush_queue();
				}
			}

			if (n_batch_get < n_batch_put)
			{
				receive_result(n_batch_put - n_batch_get >= 1);
			}
		}
		float best_n = -10.0;
		sync_cout << "info string n ";
		for (size_t i = 0; i < root_node.n_children; i++)
		{
			std::cout << root_node.value_n[i] << " ";
			if (root_node.value_n[i] > best_n)
			{
				best_n = root_node.value_n[i];
				bestMove = root_node.move_list[i];
			}
		}
		std::cout << sync_endl;
		sync_cout << "info string dup eval=" << total_dup_eval << sync_endl;
		print_pv(root_index, rootPos);
	}

	sync_cout << "bestmove " << bestMove << sync_endl;
#ifdef _DEBUG
	std::this_thread::sleep_for(std::chrono::seconds(2));
#endif

}

// �T���{�́B���񉻂��Ă���ꍇ�A������slave�̃G���g���[�|�C���g�B
// MainThread::search()��virtual�ɂȂ��Ă���think()���Ăяo�����̂ŁAMainThread::think()����
// ���̊֐����Ăяo�������Ƃ��́AThread::search()�Ƃ��邱�ƁB
void Thread::search()
{
}
#endif
