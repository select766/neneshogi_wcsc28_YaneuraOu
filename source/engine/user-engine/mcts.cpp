#ifdef USER_ENGINE

#include "mcts.h"

int dnn_get_move_index(const Position & pos, Move m);
bool dnn_write_eval_obj(dnn_eval_obj *eval_obj, const Position &pos);

TreeNode::TreeNode(TreeConfig * tree_config, TreeNode * parent, int parent_edge_index, vector<Move>& move_list, float score, vector<float>& value_p)
	//: children(), value_p(), value_n(), value_w(), value_q()
{
	this->tree_config = tree_config;
	this->parent = parent;
	this->parent_edge_index = parent_edge_index;
	this->move_list = move_list;
	this->score = score;

	auto n_children = move_list.size();
	if (n_children == 0)
	{
		this->terminal = true;
		return;
	}

	this->terminal = false;
	this->children.resize(n_children);
	this->value_p = value_p;
	this->value_n.resize(n_children);
	this->value_w.resize(n_children);
	this->value_q.resize(n_children);
	backup();
}

size_t TreeNode::n_children() const
{
	return move_list.size();
}

void TreeNode::free(TreeNode* root)
{
	// 子ノードを再帰的にdelete
	for (auto child : root->children)
	{
		if (child)
		{
			free(child);
		}
	}

	delete root;
}

void TreeNode::backup()
{
	int cur_edge = parent_edge_index;
	TreeNode* cur_node = parent;
	float cur_score = -score;
	while (cur_node != nullptr)
	{
		cur_node->value_n[cur_edge] += 1 - tree_config->virtual_loss;
		cur_node->value_w[cur_edge] += cur_score + tree_config->virtual_loss;
		cur_node->value_q[cur_edge] = cur_node->value_w[cur_edge] / cur_node->value_n[cur_edge];
		//cur_node->value_q.at(cur_edge) = cur_node->value_w[cur_edge] / cur_node->value_n[cur_edge];
		cur_edge = cur_node->parent_edge_index;
		cur_node = cur_node->parent;
		cur_score = -cur_score;
	}
}

void TreeNode::restore_virtual_loss(int edge_index)
{
	value_n[edge_index] -= tree_config->virtual_loss;
	value_w[edge_index] += tree_config->virtual_loss;
	value_q[edge_index] = value_w[edge_index] / value_n[edge_index];
	if (parent != nullptr)
	{
		parent->restore_virtual_loss(parent_edge_index);
	}
}

int TreeNode::select_edge()
{
	float n_sum = 0.01;
	for (auto n : value_n)
	{
		n_sum += n;
	}
	float n_sum_sqrt = sqrt(n_sum);

	int best_edge = 0;
	float best_value = -FLT_MAX;
	for (size_t i = 0; i < n_children(); i++)
	{
		float value = value_p[i] / (value_n[i] + 1) * tree_config->c_puct * n_sum_sqrt + value_q[i];
		if (value > best_value)
		{
			best_edge = (int)i;
			best_value = value;
		}
	}

	return best_edge;
}

bool TreeNode::select(TreeSelectResult& result)
{
	if (terminal)
	{
		// 詰みノード
		// 評価は不要で、親へ評価値を再度伝播する
		backup();
		return false;
	}

	int edge = select_edge();

	// virtual loss加算
	value_n[edge] += tree_config->virtual_loss;
	value_w[edge] -= tree_config->virtual_loss;
	value_q[edge] = value_w[edge] / value_n[edge];
	auto child = children[edge];
	result.moves.push_back(move_list[edge]);
	if (child == nullptr)
	{
		// 子ノードがまだ生成されていない
		result.final_edge_index = edge;
		result.final_node = this;
		return true;
	}
	else
	{
		return child->select(result);
	}
}

void TreeNode::play(Move* move, float* prob)
{
	// とりあえずgreedy

	Move best_move = MOVE_RESIGN;
	float best_n = -FLT_MAX;
	for (size_t i = 0; i < n_children(); i++)
	{
		if (value_n[i] > best_n)
		{
			best_n = value_n[i];
			best_move = move_list[i];
		}
	}
	*prob = 1.0;
	*move = best_move;
}

void TreeNode::get_pv(vector<Move>& pv, float& score, Move first_move)
{
	pv.push_back(first_move);
	score = this->score;
	int score_sign = 1;
	auto move_iter = std::find(move_list.begin(), move_list.end(), first_move);
	int move_index = std::distance(move_list.begin(), move_iter);
	auto cur_node = children[move_index];
	while (cur_node != nullptr)
	{
		// greedy select
		score_sign = -score_sign;
		score = cur_node->score * score_sign;
		int best_idx = -1;
		float best_n = -FLT_MAX;
		for (size_t i = 0; i < cur_node->n_children(); i++)
		{
			if (cur_node->value_n[i] > best_n)
			{
				best_n = cur_node->value_n[i];
				best_idx = i;
			}
		}
		if (best_idx < 0)
		{
			break;
		}
		pv.push_back(cur_node->move_list[best_idx]);
		cur_node = cur_node->children[best_idx];
	}
}

void TreeNode::depth_stat(vector<int>& result)
{
	result.resize(100);
	depth_stat_inner(0, result);
}

void TreeNode::depth_stat_inner(int cur_depth, vector<int>& result)
{
	result[cur_depth] += 1;
	for (auto child : children)
	{
		if (child)
		{
			child->depth_stat_inner(cur_depth + 1, result);
		}
	}
}

void pseudo_eval(Position& rootPos, TreeSelectResult& leaf, float& score, vector<Move>& move_list, vector<float>& value_p)
{
	// 同期的にダミーの評価値を返す
	StateInfo sis[256];
	// 評価すべき局面まで進める
	for (size_t i = 0; i < leaf.moves.size(); i++)
	{
		rootPos.do_move(leaf.moves[i], sis[i]);
	}
	Value raw_score = Eval::evaluate(rootPos);
	// 勝ち=1,負け=-1の値に変換
	score = (float)tanh((double)raw_score / 1200.0);//overflowが怖いのでとりあえずdoubleで計算しておく

	//1手先の各局面(の符号反転)と、現在の局面の評価値の差をsoftmaxして事前確率とみなす
	StateInfo si;
	vector<double> value_diffs;
	for (auto m : MoveList<LEGAL>(rootPos))
	{
		rootPos.do_move(m, si);
		value_diffs.push_back((double)(-Eval::evaluate(rootPos) - raw_score) / 1000.0);//10.0:初形で2g2fが80%
		move_list.push_back(m);
		rootPos.undo_move(m);
	}

	double max_value_diff = -DBL_MAX;
	for (auto vd : value_diffs)
	{
		if (vd > max_value_diff)
		{
			max_value_diff = vd;
		}
	}

	vector<double> exp_vds;
	double sum_exp_vd = 0.0;
	for (auto vd : value_diffs)
	{
		double exp_vd = exp(vd - max_value_diff);
		sum_exp_vd += exp_vd;
		exp_vds.push_back(exp_vd);
	}

	for (auto exp_vd : exp_vds)
	{
		value_p.push_back((float)(exp_vd / sum_exp_vd));
	}

	// 局面を戻す
	for (int i = (int)leaf.moves.size() - 1; i >= 0; i--)
	{
		rootPos.undo_move(leaf.moves[i]);
	}
}

void mcts_sync_eval(Position& rootPos, TreeSelectResult& leaf, float& score, vector<Move>& move_list, vector<float>& value_p,
	ipqueue<dnn_eval_obj> *eval_queue, ipqueue<dnn_result_obj> *result_queue)
{
	// 同期的に評価させる
	StateInfo sis[256];
	// 評価すべき局面まで進める
	for (size_t i = 0; i < leaf.moves.size(); i++)
	{
		rootPos.do_move(leaf.moves[i], sis[i]);
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

	score = result_obj->static_value / 32000.0F;
	dnn_move_prob best_move_prob;
	best_move_prob.prob_scaled = 0;
	for (size_t i = 0; i < result_obj->n_moves; i++)
	{
		dnn_move_prob &mp = result_obj->move_probs[i];
		move_list.push_back((Move)mp.move);
		value_p.push_back(mp.prob_scaled / 65535.0F);
	}

	result_queue->end_read();

	// 局面を戻す
	for (int i = (int)leaf.moves.size() - 1; i >= 0; i--)
	{
		rootPos.undo_move(leaf.moves[i]);
	}
}
#endif // USER_ENGINE
