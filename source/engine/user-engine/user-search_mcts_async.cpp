#include "../../extra/all.h"
#include <thread>
#include <chrono>
#include "user-search_common.h"
#include "mcts.h"

#ifdef USER_ENGINE_MCTS_ASYNC

#define MAX_UCT_CHILDREN 16//UCT�m�[�h�̎q�m�[�h���ő�

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
		Move move_list[MAX_UCT_CHILDREN];
		int value_n[MAX_UCT_CHILDREN];
		float value_w[MAX_UCT_CHILDREN];
		float value_p[MAX_UCT_CHILDREN];
		//float value_q[MAX_UCT_CHILDREN];
		//int vloss_ctr[MAX_UCT_CHILDREN];//virtual loss�������ƕ��A���ꂽ���m�F�p
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

		void clear()
		{
			used = 0;
			enough_size = true;
			memset(entries, 0, sizeof(NodeHashEntry)*uct_hash_size);
			memset(nodes, 0, sizeof(UctNode)*uct_hash_size);
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

		~NodeHash()
		{
			delete[] entries;
			delete[] nodes;
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
static int max_select = 1000;
static int pv_interval = 1000;
static int eval_count_this_search = 0;// �T���J�n����]�������m�[�h���Bnps�\���p�B�l�݂ɒB�����ꍇ���͉��Z���Ȃ��B
static int special_terminal_count_this_search = 0;// �T���J�n����A�]���֐��Ăяo���ȊO�̏I�[�m�[�h�ɓ��B�����񐔁B
static bool dnn_initialized = false;
static int block_queue_length = 2;

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
	o["PvInterval"] << Option(300, 0, 100000);//PV�o�͂���Ԋu[ms]
	o["MCTSHash"] << Option(1, 1, 1024);//MCTS�̃n�b�V���e�[�u���̃T�C�Y[GB]����B
	o["c_puct"] << Option("1.0");
	o["play_temperature"] << Option("1.0");
	o["softmax"] << Option("1.0");
	o["value_scale"] << Option("1.0");
	o["value_slope"] << Option("1.0");
	o["virtual_loss"] << Option(1, 0, 100);
	o["clear_table"] << Option(false);
	o["model"] << Option("<empty>");
	o["batch_size"] << Option(16, 1, 65536);
	o["process_per_gpu"] << Option(1, 1, 10);
	o["gpu_max"] << Option(0, -1, 16);
	o["gpu_min"] << Option(0, -1, 16);
	o["block_queue_length"] << Option(2, 1, 64);
}

// �N�����ɌĂяo�����B���Ԃ̂�����Ȃ��T���֌W�̏����������͂����ɏ������ƁB
void Search::init()
{
}

// isready�R�}���h�̉������ɌĂяo�����B���Ԃ̂����鏈���͂����ɏ������ƁB
void  Search::clear()
{
	if (node_hash)
	{
		delete node_hash;
		node_hash = nullptr;
	}
	max_select = (int)Options["NodesLimit"];
	pv_interval = (int)Options["PvInterval"];
	block_queue_length = (int)Options["block_queue_length"];
	unsigned long long hash_max_gb = (unsigned int)Options["MCTSHash"];//���̃T�C�Y�ȉ��ŁA2^n�v�f����I��
	unsigned long long hash_max_bytes = hash_max_gb * (1024 * 1024 * 1024);
	unsigned long long node_hash_size = 1;
	while (node_hash_size * (sizeof(NodeHashEntry) + sizeof(UctNode)) <= hash_max_bytes)
	{
		node_hash_size <<= 1;
	}
	node_hash_size >>= 1;

	sync_cout << "info string node hash " << (node_hash_size / (1024 * 1024)) << "M elements (Max " << MAX_UCT_CHILDREN << "moves / node)" << sync_endl;

	node_hash = new NodeHash((int)node_hash_size);
	tree_config.c_puct = (float)atof(((string)Options["c_puct"]).c_str());
	tree_config.play_temperature = (float)atof(((string)Options["play_temperature"]).c_str());
	tree_config.virtual_loss = (int)Options["virtual_loss"];
	tree_config.value_scale = (float)atof(((string)Options["value_scale"]).c_str());
	tree_config.clear_table_before_search = (bool)Options["clear_table"];

	if (!dnn_initialized)
	{
		// http://tech.ckme.co.jp/cpp/cpp_pid.shtml
		// �d�����Ȃ�mutex���̂Ƃ��ăv���Z�Xid�𗘗p
		unsigned int pid = (unsigned int)GetCurrentProcessId();//windows dependent!
		string queue_name_prefix("neneshogi_");
		queue_name_prefix += std::to_string(pid);
		sync_cout << "info string starting dnn process" << sync_endl;
		string dnn_system_command("python -m neneshogi.process_pyshogieval_frontend");
		dnn_system_command += " ";
		dnn_system_command += (string)Options["model"];
		dnn_system_command += " --batch_size ";
		dnn_system_command += std::to_string((int)Options["batch_size"]);
		dnn_system_command += " --softmax ";
		dnn_system_command += (string)Options["softmax"];
		dnn_system_command += " --value_slope ";
		dnn_system_command += (string)Options["value_slope"];
		dnn_system_command += " --gpu_min ";
		dnn_system_command += std::to_string((int)Options["gpu_min"]);
		dnn_system_command += " --gpu_max ";
		dnn_system_command += std::to_string((int)Options["gpu_max"]);
		dnn_system_command += " --process_per_gpu ";
		dnn_system_command += std::to_string((int)Options["process_per_gpu"]);
		dnn_system_command += " --queue_prefix ";
		dnn_system_command += queue_name_prefix;
		// ���̃v���Z�X���I��������ADNN�v���Z�X���I������
		dnn_system_command += " --host_pid ";
		dnn_system_command += std::to_string(pid);
		if (system(dnn_system_command.c_str()) == 0)
		{
			sync_cout << "info string started dnn process" << sync_endl;
			dnn_initialized = true;
		}
		else
		{
			sync_cout << "info string FAILED to start dnn process!" << sync_endl;
		}
		init_dnn_queues(queue_name_prefix);
	}

	show_error_if_dnn_queue_fail();

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

// tree��backup����B
void backup_tree(float leaf_score, dnn_table_index &path)
{
	float score = leaf_score;

	// tree�����ǂ�l���X�V
	for (int i = path.path_length - 2; i >= 0; i--)
	{
		//score = -score;
		score = score * -0.99F;//�����鎞�͂�蒷���l�݋؁A�ǂ��Ƃ��͒Z���l�݋؂�I�Ԃ悤����
		UctNode &inner_node = node_hash->nodes[path.path_indices[i]];
		uint16_t edge = path.path_child_indices[i];
		int new_value_n = inner_node.value_n[edge] + 1 - tree_config.virtual_loss;
		inner_node.value_n[edge] = new_value_n;
		float new_value_w = inner_node.value_w[edge] + score + tree_config.virtual_loss;
		inner_node.value_w[edge] = new_value_w;
		// inner_node.vloss_ctr[edge]--;
		// inner_node.value_q[edge] = new_value_w / new_value_n;
		inner_node.value_n_sum += 1 - tree_config.virtual_loss;
	}
}

bool operator<(const dnn_move_prob& left, const dnn_move_prob& right) {
	// �m���ō~���\�[�g�p
	return left.prob_scaled > right.prob_scaled;
}

void update_on_dnn_result(dnn_result_obj *result_obj)
{
	dnn_table_index &path = result_obj->index;
	// ���[�m�[�h�̕]�����L�^
	UctNode &leaf_node = node_hash->nodes[path.path_indices[path.path_length - 1]];
	leaf_node.evaled = true;
	// ���O�m���Ń\�[�g���A��� MAX_UCT_CHILDREN �����L�^
	int n_moves_use = result_obj->n_moves;
	if (n_moves_use > MAX_UCT_CHILDREN)
	{
		std::sort(&result_obj->move_probs[0], &result_obj->move_probs[result_obj->n_moves]);
		n_moves_use = MAX_UCT_CHILDREN;
	}
	for (int i = 0; i < n_moves_use; i++)
	{
		dnn_move_prob &dmp = result_obj->move_probs[i];
		leaf_node.move_list[i] = (Move)dmp.move;
		leaf_node.value_p[i] = dmp.prob_scaled / 65535.0F;
		// n, w, q��0����������Ă���
	}
	leaf_node.n_children = n_moves_use;
	float score = result_obj->static_value / 32000.0F * tree_config.value_scale; // [-1.0, 1.0]
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
	leaf_node.score = score;
	backup_tree(score, path);
}

// ���[�m�[�h���]���s�v�m�[�h�������ꍇ
void update_on_terminal(float leaf_score, dnn_table_index &path)
{
	special_terminal_count_this_search++;
	backup_tree(leaf_score, path);
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

	eval_count_this_search += receive_count;

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
	float n_sum_sqrt = sqrt((float)node.value_n_sum) + 0.001F;//���S��0���ƍŏ���1�肪���O�m���ɉ���Ȃ��Ȃ�
	int best_index = 0;
	float best_value = -100.0F;
	for (size_t i = 0; i < node.n_children; i++)
	{
		float value_n = (float)node.value_n[i];
		float value_u = node.value_p[i] / (value_n + 1) * tree_config.c_puct * n_sum_sqrt;
		float value_q = node.value_w[i] / (value_n + 1e-8F);//0���Z���
		float value_sum = value_q + value_u;
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
int current_max_depth = 0;

void mcts_select(int node_index, dnn_table_index &path, Position &pos)
{
	if (path.path_length >= MAX_SEARCH_PATH_LENGTH)
	{
		// �����͗l�̋؂ȂǂŋN���邩������Ȃ��̂ňꉞ�΍�
		// ���������Ƃ݂Ȃ��ďI������
		update_on_terminal(0.0, path);
		return;
	}

	UctNode &node = node_hash->nodes[node_index];
	if (node.terminal)
	{
		// �l�݃m�[�h
		// �]���͕s�v�ŁA�e�֕]���l���ēx�`�d����
		update_on_terminal(-1.0, path);
		return;
	}

	if (path.path_length > 1) // ���[�g�m�[�h���̂�����Ƃ͔��肵�Ȃ�
	{
		RepetitionState rep_state = pos.is_repetition(pos.game_ply() - path.path_length);
		if (rep_state != RepetitionState::REPETITION_NONE)
		{
			float score;
			switch (rep_state)
			{
			case REPETITION_WIN:
			case REPETITION_SUPERIOR:
				score = 1.0;
				break;
			case REPETITION_LOSE:
			case REPETITION_INFERIOR:
				score = -1.0;
				break;
			default:
				score = 0.0;
				break;
			}

			update_on_terminal(score, path);
			return;
		}
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
	// node.vloss_ctr[edge]++;
	// node.value_q[edge] = node.value_w[edge] / node.value_n[edge];

	Move m = node.move_list[edge];
	StateInfo si;
	pos.do_move(m, si);

	// �q�m�[�h��I�����邩����
	bool created;
	int child_index = node_hash->find_or_create_index(pos, &created);
	path.path_child_indices[path.path_length - 1] = edge;
	path.path_indices[path.path_length] = child_index;
	path.path_length++;
	if (path.path_length > current_max_depth)
	{
		current_max_depth = path.path_length;
	}

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

// pv�擾�Bwinrate�̓��[�g�m�[�h�ł�bestMove�̏����Bmate_in�́A�ǂ݋؂̖��[���l�݂̂Ƃ��̎萔�B���[�g���l��ł�����0�B�l�܂Ȃ��Ƃ����̒l�B
void get_pv(int cur_index, vector<Move> &pv, Position &pos, bool root, float &winrate, int &mate_in)
{
	UctNode *node = &node_hash->nodes[cur_index];
	if (node->terminal)
	{
		if (root)
		{
			winrate = -1.0F;
		}
		mate_in = 0;
		return;
	}
	int best_n = -1;
	Move bestMove = MOVE_RESIGN;
	int best_child_i = 0;
	for (size_t i = 0; i < node->n_children; i++)
	{
		if (node->value_n[i] > best_n)
		{
			best_n = node->value_n[i];
			bestMove = node->move_list[i];
			best_child_i = i;
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
			get_pv(child_index, pv, pos, false, winrate, mate_in);
			mate_in++;
		}
		else
		{
			// �ǂ݋؂��r�؂ꂽ
			// �l�܂Ȃ����̂Ƃ��Ĉ���
			mate_in = -1000;
		}
		pos.undo_move(bestMove);
	}
	if (root)
	{
		winrate = node->value_w[best_child_i] / node->value_n[best_child_i];
	}
}

int winrate_to_cp(float winrate)
{
	// ����-1.0~1.0��]���l�ɕϊ�����
	// tanh�̋t�֐� (1/2)*log((1+x)/(1-x))
	// 1, -1�Ȃ�inf�ɂȂ�̂Ŋۂ߂�
	// 1��=100�����A���������]���֐�������Ă��Ȃ����߃X�P�[���͂�����ۂ���������̂ɂ���ق��Ȃ�
	float v = (log1pf(winrate) - log1pf(-winrate)) * 600;
	if (v < -30000)
	{
		v = -30000;
	}
	else if (v > 30000)
	{
		v = 30000;
	}
	return (int)v;
}

void print_pv(int root_index, Position &rootPos)
{
	UctNode *root_node = &node_hash->nodes[root_index];
	vector<Move> pv;
	float winrate;
	int mate_in;
	get_pv(root_index, pv, rootPos, true, winrate, mate_in);
	int elapsed_ms = Time.elapsed();
	int nps = eval_count_this_search * 1000 / max(elapsed_ms, 1);//0���Z���
	int hashfull = (int)((long long)node_hash->used * 1000 / node_hash->uct_hash_size);
	sync_cout << "info nodes " << root_node->value_n_sum << " depth " << pv.size();
	if (mate_in >= 0)
	{
		char* sign = "";
		if (mate_in % 2 == 0)
		{
			// �l�܂��������̂Ƃ��̓}�C�i�X������(�l��ł���0��̂Ƃ���-��t����)
			sign = "-";
		}
		cout << " score mate " << sign << mate_in;
	}
	else
	{
		cout << " score cp " << winrate_to_cp(winrate);
	}

	cout << " time " << elapsed_ms << " nps " << nps << " hashfull " << hashfull << " pv";
	for (auto m : pv)
	{
		std::cout << " " << m;
	}
	std::cout << sync_endl;
}

void select_best_move(Position &rootPos, UctNode &root_node, Move &bestMove, Move &ponderMove)
{
	int best_n = -1;
	sync_cout << "info string n ";
	int best_child_index = -1;
	// greedy
	// TODO: play temperature��
	for (size_t i = 0; i < root_node.n_children; i++)
	{
		std::cout << root_node.value_n[i] << "(" << root_node.move_list[i] << ") ";
		if (root_node.value_n[i] > best_n)
		{
			best_child_index = i;
			best_n = root_node.value_n[i];
			bestMove = root_node.move_list[i];
		}
	}
	std::cout << sync_endl;

	if (best_child_index >= 0)
	{
		// �������w������̋ǖʂ�greedy�Ɏw�����I��ponder�ɂ���
		StateInfo si;
		rootPos.do_move(bestMove, si);
		int child_index = node_hash->find_index(rootPos);
		if (child_index >= 0)
		{
			auto &best_child_node = node_hash->nodes[child_index];
			int best_child_n = -1;
			for (size_t i = 0; i < best_child_node.n_children; i++)
			{
				int node_n = best_child_node.value_n[i];
				if (node_n > best_child_n)
				{
					best_child_n = node_n;
					ponderMove = best_child_node.move_list[i];
				}
			}
		}
		rootPos.undo_move(bestMove);
	}
}

std::atomic_bool in_search_time;

// �T���J�n���ɌĂяo�����B
// ���̊֐����ŏ��������I��点�Aslave�X���b�h���N������Thread::search()���Ăяo���B
// ���̂���slave�X���b�h���I�������A�x�X�g�Ȏw�����Ԃ����ƁB
void MainThread::think()
{
	Time.init(Search::Limits, rootPos.side_to_move(), rootPos.game_ply());
	long long next_pv_time = 0;
	in_search_time = true;
	if (tree_config.clear_table_before_search)
	{
		node_hash->clear();
	}
	int root_index = get_or_create_root(rootPos);
	UctNode &root_node = node_hash->nodes[root_index];
	Move bestMove = MOVE_RESIGN;
	Move ponderMove = MOVE_RESIGN;
	int n_select = root_node.value_n_sum;
	if (!root_node.terminal)
	{
		eval_count_this_search = 0;
		special_terminal_count_this_search = 0;

		// ���O�m���\��
		sync_cout << "info string prob ";
		float best_p = -10.0;
		Move best_p_move = MOVE_RESIGN;
		for (size_t i = 0; i < root_node.n_children; i++)
		{
			std::cout << root_node.move_list[i] << "(" << (int)(root_node.value_p[i] * 100) << "%) ";
			if (root_node.value_p[i] > best_p)
			{
				best_p = root_node.value_p[i];
				best_p_move = root_node.move_list[i];
			}
		}
		std::cout << sync_endl;
		sync_cout << "info score cp " << winrate_to_cp(best_p) << " pv " << best_p_move << sync_endl;

		total_dup_eval = 0;
		auto timer_thread = std::thread([] {
			while (!Threads.stop && (Threads.ponder || Time.elapsed() < Time.optimum()) && in_search_time)
			{
				sleep(10);
			}
			// Time.elapsed() < Time.optimum()�̏ꍇ�Aponder�ŊJ�n���Ă���̎��ԂɂȂ�B�t�B�b�V���[�N���b�N���[���Ȃ炱��Ŗ��Ȃ��B
			// �b�ǂݏ�Ԃ��Ɩ��ʂɂȂ��Ă��܂��B
			// ����Œ�~�t���O�𗧂Ă���ADNN�]�����Ԃ��Ă���܂ő҂K�v������̂�Time.maximum()�͊댯�B
			in_search_time = false;
		});
		// �T�����ԓ��͖؍\���T��������B�����ɕ]�����ʂ�����B���Ԑ؂�ɂȂ�����flush���ē����ςݕ]���o�b�`������B
		bool no_more_search = false;
		while (true)
		{
			if (in_search_time && (n_select < max_select))
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
			}
			else
			{
				if (!no_more_search)
				{
					// ����ȏ㓊�����Ȃ��̂�flush����
					flush_queue();
				}
				no_more_search = true;
			}

			int pending_batches = n_batch_put - n_batch_get;
			if (no_more_search && pending_batches == 0)
			{
				break;
			}
			if (pending_batches > 0)
			{
				bool block = false;
				if (root_node.value_n_sum < 10000)
				{
					// �T���񐔂����Ȃ������ɕ����̃o�b�`��]���҂��ɂ���ƁA�d���������Ȃ�o�C�A�X���傫���Ȃ�
					block = true;
				}
				else if (pending_batches >= block_queue_length)
				{
					block = true;
				}

				if (receive_result(block || no_more_search))
				{
					// �p�ɂɎ����擾������̂����ʂ����Ȃ̂ł�����
					auto elapsed = Time.elapsed();
					if (elapsed >= next_pv_time)
					{
						print_pv(root_index, rootPos);
						next_pv_time += pv_interval;
					}
				}
			}
		}

		in_search_time = false;//n_select < max_select�̏�����while�𔲂��Ă�timer_thread���I�������邽��
		timer_thread.join();

		select_best_move(rootPos, root_node, bestMove, ponderMove);

		sync_cout << "info string dup eval=" << total_dup_eval << " special=" << special_terminal_count_this_search << sync_endl;
		sync_cout << "info string max depth=" << current_max_depth << sync_endl;
		print_pv(root_index, rootPos);
	}

#ifdef _DEBUG
	std::this_thread::sleep_for(std::chrono::seconds(5));
#endif
	while (Threads.ponder && !Threads.stop)
	{
		// ������Threads.stop�͎��ۂ�stop�R�}���h���������Ƃ�\���Ȃ��Ƃ����Ȃ��B�T���I�����ԂȂǂŏ���������ƈᔽ�ɂȂ�B
		// ponder���͕Ԃ��Ă͂����Ȃ�
		sleep(1);
	}
	sync_cout << "bestmove " << bestMove;
	if (ponderMove != MOVE_RESIGN)
	{
		cout << " ponder " << ponderMove;
	}
	cout << sync_endl;

}

// �T���{�́B���񉻂��Ă���ꍇ�A������slave�̃G���g���[�|�C���g�B
// MainThread::search()��virtual�ɂȂ��Ă���think()���Ăяo�����̂ŁAMainThread::think()����
// ���̊֐����Ăяo�������Ƃ��́AThread::search()�Ƃ��邱�ƁB
void Thread::search()
{
}
#endif
