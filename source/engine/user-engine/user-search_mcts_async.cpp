#include "../../extra/all.h"
#include <thread>
#include <chrono>
#include "user-search_common.h"
#include "mcts.h"

#ifdef USER_ENGINE_MCTS_ASYNC

#define MAX_UCT_CHILDREN 16//UCTノードの子ノード数最大

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
		DupEvalChain *dup_eval_chain;//複数回評価が呼ばれたとき、ここにリストをつなげて各経路でbackupする。
		float score;
		int n_children;
		Move move_list[MAX_UCT_CHILDREN];
		int value_n[MAX_UCT_CHILDREN];
		float value_w[MAX_UCT_CHILDREN];
		float value_p[MAX_UCT_CHILDREN];
		//float value_q[MAX_UCT_CHILDREN];
		//int vloss_ctr[MAX_UCT_CHILDREN];//virtual lossがちゃんと復帰されたか確認用
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
static int eval_count_this_search = 0;// 探索開始から評価したノード数。nps表示用。詰みに達した場合等は加算しない。
static int special_terminal_count_this_search = 0;// 探索開始から、評価関数呼び出し以外の終端ノードに到達した回数。
static bool dnn_initialized = false;
static int block_queue_length = 2;

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
	o["PvInterval"] << Option(300, 0, 100000);//PV出力する間隔[ms]
	o["MCTSHash"] << Option(1, 1, 1024);//MCTSのハッシュテーブルのサイズ[GB]上限。
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

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init()
{
}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
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
	unsigned long long hash_max_gb = (unsigned int)Options["MCTSHash"];//このサイズ以下で、2^n要素数を選択
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
		// 重複しないmutex名称としてプロセスidを利用
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
		// このプロセスが終了したら、DNNプロセスも終了する
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

// treeのbackup操作。
void backup_tree(float leaf_score, dnn_table_index &path)
{
	float score = leaf_score;

	// treeをたどり値を更新
	for (int i = path.path_length - 2; i >= 0; i--)
	{
		//score = -score;
		score = score * -0.99F;//逃げる時はより長い詰み筋、追うときは短い詰み筋を選ぶよう調整
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
	// 確率で降順ソート用
	return left.prob_scaled > right.prob_scaled;
}

void update_on_dnn_result(dnn_result_obj *result_obj)
{
	dnn_table_index &path = result_obj->index;
	// 末端ノードの評価を記録
	UctNode &leaf_node = node_hash->nodes[path.path_indices[path.path_length - 1]];
	leaf_node.evaled = true;
	// 事前確率でソートし、上位 MAX_UCT_CHILDREN だけ記録
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
		// n, w, qは0初期化されている
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
	// 新規展開ノードがmateだったときの処理
	UctNode &leaf_node = node_hash->nodes[path.path_indices[path.path_length - 1]];
	leaf_node.evaled = true;
	leaf_node.terminal = true;
	float score = -1.0F;
	leaf_node.score = score;
	backup_tree(score, path);
}

// 末端ノードが評価不要ノードだった場合
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

	// 局面評価
	dnn_table_index path;
	path.path_length = 1;
	path.path_indices[0] = index;
	bool not_mate = enqueue_pos(pos, path);
	if (not_mate)
	{
		// 評価待ち
		flush_queue();
		receive_result(true);
	}
	else
	{
		// 詰んでいて評価対象にならない
		update_on_mate(path);
	}

	return index;
}

int select_edge(UctNode &node)
{
	float n_sum_sqrt = sqrt((float)node.value_n_sum) + 0.001F;//完全に0だと最初の1手が事前確率に沿わなくなる
	int best_index = 0;
	float best_value = -100.0F;
	for (size_t i = 0; i < node.n_children; i++)
	{
		float value_n = (float)node.value_n[i];
		float value_u = node.value_p[i] / (value_n + 1) * tree_config.c_puct * n_sum_sqrt;
		float value_q = node.value_w[i] / (value_n + 1e-8F);//0除算回避
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
		// 千日手模様の筋などで起こるかもしれないので一応対策
		// 引き分けとみなして終了する
		update_on_terminal(0.0, path);
		return;
	}

	UctNode &node = node_hash->nodes[node_index];
	if (node.terminal)
	{
		// 詰みノード
		// 評価は不要で、親へ評価値を再度伝播する
		update_on_terminal(-1.0, path);
		return;
	}

	if (path.path_length > 1) // ルートノード自体を千日手とは判定しない
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
		// ノードが評価中だった場合
		// virtual lossがあるので、評価が終わったときに追加でbackupを呼ぶようにする
		// link listにつなぐ
		DupEvalChain *dec = new DupEvalChain();
		memcpy(&dec->path, &path, sizeof(dnn_table_index));
		dec->next = node.dup_eval_chain;
		node.dup_eval_chain = dec;
		total_dup_eval++;
		dup_eval_flag = true;
		return;
	}

	// エッジ選択
	int edge = select_edge(node);

	// virtual loss加算
	node.value_n[edge] += tree_config.virtual_loss;
	node.value_n_sum += tree_config.virtual_loss;
	node.value_w[edge] -= tree_config.virtual_loss;
	// node.vloss_ctr[edge]++;
	// node.value_q[edge] = node.value_w[edge] / node.value_n[edge];

	Move m = node.move_list[edge];
	StateInfo si;
	pos.do_move(m, si);

	// 子ノードを選択するか生成
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
		// 新規子ノードなので、評価
		bool not_mate = enqueue_pos(pos, path);
		if (not_mate)
		{
			// 評価待ち
			// 非同期に処理される
		}
		else
		{
			// 詰んでいて評価対象にならない
			update_on_mate(path);
		}
	}
	else
	{
		// 再帰的に探索
		mcts_select(child_index, path, pos);
	}

	pos.undo_move(m);
}

// pv取得。winrateはルートノードでのbestMoveの勝率。mate_inは、読み筋の末端が詰みのときの手数。ルートが詰んでいたら0。詰まないとき負の値。
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
			// 読み筋が途切れた
			// 詰まないものとして扱う
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
	// 勝率-1.0~1.0を評価値に変換する
	// tanhの逆関数 (1/2)*log((1+x)/(1-x))
	// 1, -1ならinfになるので丸める
	// 1歩=100だが、そういう評価関数を作っていないためスケールはそれっぽく見えるものにするほかない
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
	int nps = eval_count_this_search * 1000 / max(elapsed_ms, 1);//0除算回避
	int hashfull = (int)((long long)node_hash->used * 1000 / node_hash->uct_hash_size);
	sync_cout << "info nodes " << root_node->value_n_sum << " depth " << pv.size();
	if (mate_in >= 0)
	{
		char* sign = "";
		if (mate_in % 2 == 0)
		{
			// 詰まされる方向のときはマイナスをつける(詰んでいて0手のときも-を付ける)
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
	// TODO: play temperature版
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
		// 自分が指した後の局面でgreedyに指し手を選びponderにする
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

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
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

		// 事前確率表示
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
			// Time.elapsed() < Time.optimum()の場合、ponderで開始してからの時間になる。フィッシャークロックルールならこれで問題ない。
			// 秒読み状態だと無駄になってしまう。
			// これで停止フラグを立てた後、DNN評価が返ってくるまで待つ必要があるのでTime.maximum()は危険。
			in_search_time = false;
		});
		// 探索時間内は木構造探索をする。同時に評価結果を回収。時間切れになったらflushして投入済み評価バッチを回収。
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
					// これ以上投入しないのでflushする
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
					// 探索回数が少ないうちに複数のバッチを評価待ちにすると、重複が多くなりバイアスが大きくなる
					block = true;
				}
				else if (pending_batches >= block_queue_length)
				{
					block = true;
				}

				if (receive_result(block || no_more_search))
				{
					// 頻繁に時刻取得をするのも無駄そうなのでここで
					auto elapsed = Time.elapsed();
					if (elapsed >= next_pv_time)
					{
						print_pv(root_index, rootPos);
						next_pv_time += pv_interval;
					}
				}
			}
		}

		in_search_time = false;//n_select < max_selectの条件でwhileを抜けてもtimer_threadを終了させるため
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
		// ここのThreads.stopは実際にstopコマンドが来たことを表さないといけない。探索終了時間などで書き換えると違反になる。
		// ponder中は返してはいけない
		sleep(1);
	}
	sync_cout << "bestmove " << bestMove;
	if (ponderMove != MOVE_RESIGN)
	{
		cout << " ponder " << ponderMove;
	}
	cout << sync_endl;

}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
// MainThread::search()はvirtualになっていてthink()が呼び出されるので、MainThread::think()から
// この関数を呼び出したいときは、Thread::search()とすること。
void Thread::search()
{
}
#endif
