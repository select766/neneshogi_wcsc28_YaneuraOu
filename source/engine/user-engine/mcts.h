#include "../../extra/all.h"
#include "user-search_common.h"
#include "../../../ipqueue/ipqueue/ipqueue.h"
#include "dnn_eval_obj.h"

#ifdef USER_ENGINE

class TreeConfig
{
public:
	float c_puct;
	float play_temperature;
	float virtual_loss;
	float value_scale;
	bool clear_table_before_search;
};

class TreeSelectResult;

class TreeNode
{
public:
	TreeConfig* tree_config;
	TreeNode* parent;
	int parent_edge_index;
	vector<Move> move_list;
	float score;
	vector<TreeNode*> children;
	vector<float> value_n;
	vector<float> value_w;
	vector<float> value_q;
	vector<float> value_p;
	bool terminal;

	TreeNode(TreeConfig* tree_config, TreeNode* parent, int parent_edge_index,
		vector<Move>& move_list, float score, vector<float>& value_p);
	static void free(TreeNode* root);
	size_t n_children() const;
	void backup();
	void restore_virtual_loss(int edge_index);
	int select_edge();
	bool select(TreeSelectResult& result);
	void play(Move* move, float* prob);
	void get_pv(vector<Move>& pv, float& score, Move first_move);
	void depth_stat(vector<int>& result);
	void depth_stat_inner(int cur_depth, vector<int>& result);
};

class TreeSelectResult
{
public:
	TreeNode * final_node;
	int final_edge_index;
	vector<Move> moves;
};

void pseudo_eval(Position& rootPos, TreeSelectResult& leaf, float& score, vector<Move>& move_list, vector<float>& value_p);
void mcts_sync_eval(Position& rootPos, TreeSelectResult& leaf, float& score, vector<Move>& move_list, vector<float>& value_p,
	ipqueue<dnn_eval_obj> *eval_queue, ipqueue<dnn_result_obj> *result_queue);
#endif // USER_ENGINE
