#include "pyinterface.h"
#include "thread.h"
#include <algorithm>
#include <functional>

bool PyPosition::initialized = false;
static MainThread *main_thread = nullptr;

PyPosition::PyPosition() : psv_move(MOVE_NONE), rnd(std::random_device()())
{
	if (!initialized)
	{
		// main関数と同等の初期化をする
		Bitboards::init();
		Position::init();
		Search::init();
		Threads.init(1);
		Eval::init();
	}
}

void PyPosition::set(std::string sfen)
{
	pos.set(sfen, &init_state, Threads.main());
}

void PyPosition::set_hirate()
{
	set(SFEN_HIRATE);
}

int PyPosition::side_to_move() const
{
	return (int)pos.side_to_move();
}

int PyPosition::game_ply() const
{
	return pos.game_ply();
}

const std::string PyPosition::sfen() const
{
	return pos.sfen();
}

void PyPosition::do_move(PyMove m)
{
	moves[pos.game_ply()] = m.m;
	pos.do_move(m.m, state[pos.game_ply()]);
}

void PyPosition::undo_move()
{
	pos.undo_move(moves[pos.game_ply() - 1]);
}

void PyPosition::do_null_move()
{
	pos.do_null_move(state[pos.game_ply()]);
}

void PyPosition::undo_null_move()
{
	pos.undo_null_move();
}

uint64_t PyPosition::key() const
{
	return (uint64_t)pos.key();
}

bool PyPosition::in_check() const
{
	return pos.in_check();
}

bool PyPosition::is_mated() const
{
	return pos.is_mated();
}

bool PyPosition::legal(PyMove m) const
{
	return pos.pseudo_legal(m.m) && pos.legal(m.m);
}

Value PyPosition::_search(int depth, Value alpha, Value beta)
{
	if (depth > 0)
	{
		bool mated = true;
		for (auto m : MoveList<LEGAL>(pos))
		{
			mated = false;

			StateInfo si;
			pos.do_move(m, si);
			Value v = -_search(depth - 1, -beta, -alpha);
			pos.undo_move(m);
			if (v > alpha)
			{
				alpha = v;
			}
			if (alpha > beta)
			{
				break;
			}
		}
		if (mated)
		{
			return -VALUE_MATE;
		}
		return alpha;
	}
	else
	{
		return Eval::evaluate(pos);
	}
}

PyMove PyPosition::search(int depth)
{
	std::vector<Move> move_list;
	for (auto m : MoveList<LEGAL>(pos))
	{
		move_list.push_back(m);
	}

	Move best_move = MOVE_RESIGN;
	if (!move_list.empty())
	{
		// 評価値が同じ場合にランダム性を出すためにシャッフル
		std::shuffle(move_list.begin(), move_list.end(), rnd);

		Value best_value = -VALUE_INFINITE;
		for (auto m : move_list)
		{
			StateInfo si;
			pos.do_move(m, si);
			Value v = -_search(depth - 1, -VALUE_INFINITE, VALUE_INFINITE);
			pos.undo_move(m);
			if (v > best_value)
			{
				best_value = v;
				best_move = m;
			}
		}
	}
	return PyMove(best_move);
}

py::array_t<uint32_t> PyPosition::get_board()
{
	uint32_t buf[81];
	for (int i = 0; i < 81; i++) {
		buf[i] = pos.piece_on((Square)i);
	}
	// Positionをフレンドクラスにしてpos.boardを直接渡してもよい
	return py::array_t<uint32_t>(
		py::buffer_info(
			buf,
			sizeof(uint32_t),
			py::format_descriptor<uint32_t>::format(),
			1,
			{ 81 },
			{ sizeof(uint32_t) }
		)
		);
}

py::array_t<uint32_t> PyPosition::get_hand()
{
	uint32_t buf[14];
	Hand hand_black = pos.hand_of(BLACK);
	buf[0] = hand_count(hand_black, PAWN);
	buf[1] = hand_count(hand_black, LANCE);
	buf[2] = hand_count(hand_black, KNIGHT);
	buf[3] = hand_count(hand_black, SILVER);
	buf[4] = hand_count(hand_black, BISHOP);
	buf[5] = hand_count(hand_black, ROOK);
	buf[6] = hand_count(hand_black, GOLD);
	Hand hand_white = pos.hand_of(WHITE);
	buf[7] = hand_count(hand_white, PAWN);
	buf[8] = hand_count(hand_white, LANCE);
	buf[9] = hand_count(hand_white, KNIGHT);
	buf[10] = hand_count(hand_white, SILVER);
	buf[11] = hand_count(hand_white, BISHOP);
	buf[12] = hand_count(hand_white, ROOK);
	buf[13] = hand_count(hand_white, GOLD);
	// bufは(ドキュメント上に記載がないが)ここでコピーされるのでスタック上で問題ない
	return py::array_t<uint32_t>(
		py::buffer_info(
			buf,
			sizeof(uint32_t),
			py::format_descriptor<uint32_t>::format(),
			2,
			{ 2, 7 },
			{ sizeof(uint32_t) * 7, sizeof(uint32_t) }
		)
		);
}

std::vector<PyMove> PyPosition::genereate_move_list()
{
	std::vector<PyMove> move_list;
	for (auto m : MoveList<LEGAL>(pos))
	{
		move_list.push_back(PyMove(m));
	}
	return move_list;
}

bool PyPosition::set_from_packed_sfen(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> packed_sfen)
{
	auto r = packed_sfen.unchecked<1>();
	const PackedSfen* sfen = reinterpret_cast<const PackedSfen*>(r.data(0));
	return pos.set_from_packed_sfen(*sfen, &init_state, Threads.main()) == 0;
	// gamePly = 0となるので注意
}

bool PyPosition::set_from_packed_sfen_value(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> packed_sfen)
{
	auto r = packed_sfen.unchecked<1>();
	const PackedSfen* sfen = reinterpret_cast<const PackedSfen*>(r.data(0));
	bool pos_ok = pos.set_from_packed_sfen(*sfen, &init_state, Threads.main()) == 0;
	const int16_t* score = reinterpret_cast<const int16_t*>(r.data(32));
	psv_score = *score;
	const uint16_t* move = reinterpret_cast<const uint16_t*>(r.data(34));
	psv_move = PyMove::from_int(*move);
	const uint16_t* gamePly = reinterpret_cast<const uint16_t*>(r.data(36));
	psv_game_ply = *gamePly;
	const int8_t* game_result = reinterpret_cast<const int8_t*>(r.data(38));
	psv_game_result = *game_result;
	return pos_ok;
}

PyMove::PyMove(Move m)
{
	this->m = m;
}

PyMove PyMove::make_move(int32_t from, int32_t to)
{
	return PyMove(::make_move((Square)from, (Square)to));
}

PyMove PyMove::make_move_promote(int32_t from, int32_t to)
{
	return PyMove(::make_move_promote((Square)from, (Square)to));
}

PyMove PyMove::make_move_drop(uint32_t pt, int32_t to)
{
	return PyMove(::make_move_drop((Piece)pt, (Square)to));
}

PyMove PyMove::from_int(uint16_t move)
{
	return PyMove((Move)move);
}

PyMove PyMove::from_usi(const std::string str)
{
	return PyMove(move_from_usi(str));
}

bool PyMove::is_ok()
{
	return ::is_ok(m);
}

bool PyMove::is_drop()
{
	return ::is_drop(m);
}

bool PyMove::is_promote()
{
	return ::is_promote(m);
}

int PyMove::special()
{
	// 特殊なMoveの種類を表す番号を返す。
	// MOVE_NULL: 1, MOVE_RESIGN: 2, MOVE_WIN: 3
	return ((int)m) & 0x7f;
}

int PyMove::move_from()
{
	return (int)::move_from(m);
}

int PyMove::move_to()
{
	return (int)::move_to(m);
}

int PyMove::move_dropped_piece()
{
	return (int)::move_dropped_piece(m);
}

std::string PyMove::to_usi_string()
{
	return ::to_usi_string(m);
}

uint16_t PyMove::to_int()
{
	return (uint16_t)m;
}

bool PyMove::__eq__(const PyMove m)
{
	return this->m == m.m;
}

PyMove PyMove::MOVE_NONE = PyMove::from_int((Move)::MOVE_NONE);
PyMove PyMove::MOVE_NULL = PyMove::from_int((Move)::MOVE_NULL);
PyMove PyMove::MOVE_RESIGN = PyMove::from_int((Move)::MOVE_RESIGN);
PyMove PyMove::MOVE_WIN = PyMove::from_int((Move)::MOVE_WIN);

DNNConverter::DNNConverter(int _format_board, int _format_move) :
	format_board(_format_board), format_move(_format_move)
{
}

py::tuple DNNConverter::board_shape() const
{
	return py::make_tuple(86, 9, 9);
}

py::tuple DNNConverter::move_shape() const
{
	return py::make_tuple(139, 9, 9);
}

void fill_channel(float* buf, int ch, float value)
{
	for (Square i = SQ_ZERO; i < SQ_NB; i++) {
		buf[ch * SQ_NB + i] = value;
	}
}

void fill_channel_range(float* buf, int ch_begin, int ch_end, float value)
{
	while (ch_begin < ch_end)
	{
		fill_channel(buf, ch_begin++, value);
	}
}

py::array_t<float> DNNConverter::get_board_array(const PyPosition & pos) const
{
	/*
	* Ponanza (SDT5)の資料を参考に作成
	* 盤上の駒14チャンネル *二人
	* 持ち駒は、枚数分のチャンネル(金なら4)を用意して1で埋めていく(歩は最大8枚)
	* 歩*8,香車*4,桂馬*4,銀*4,角*2,飛車*2,金*4=28*二人
	* 手数（手数/256の連続値） 1次元
	* 王手かどうか 1次元
	* 後手番の際は、盤面・駒の所属を反転して先手番の状態にする。
	*/
	const Position& ppos = pos.pos;
	float buf[86 * 9 * 9] = {};
	if (pos.side_to_move() == BLACK) {
		for (Square i = SQ_ZERO; i < SQ_NB; i++) {
			Piece p = ppos.piece_on(i);
			if (p == PIECE_ZERO) {
				continue;
			}
			int ch;
			if (color_of(p) == BLACK) {
				ch = p - B_PAWN;
			}
			else {
				ch = p - W_PAWN + 14;
			}
			buf[ch * SQ_NB + i] = 1;
		}
	}
	else {
		for (Square i = SQ_ZERO; i < SQ_NB; i++) {
			Piece p = ppos.piece_on(i);
			if (p == PIECE_ZERO) {
				continue;
			}
			int ch;
			// 先手後手入れ替え+座標回転
			if (color_of(p) == BLACK) {
				ch = p - B_PAWN + 14;
			}
			else {
				ch = p - W_PAWN;
			}
			buf[ch * SQ_NB + Inv(i)] = 1;
		}

	}

	int ch_ofs = 28;
	Hand hands[2] = { ppos.hand_of(ppos.side_to_move()), ppos.hand_of(~ppos.side_to_move()) };
	for (int i = 0; i < 2; i++) {
		Hand hand = hands[i];
		//歩は最大8枚
		fill_channel_range(buf, ch_ofs, (std::min)(ch_ofs + hand_count(hand, PAWN), 8), 1.0);
		ch_ofs += 8;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, LANCE), 1.0);
		ch_ofs += 4;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, KNIGHT), 1.0);
		ch_ofs += 4;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, SILVER), 1.0);
		ch_ofs += 4;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, BISHOP), 1.0);
		ch_ofs += 2;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, ROOK), 1.0);
		ch_ofs += 2;
		fill_channel_range(buf, ch_ofs, ch_ofs + hand_count(hand, GOLD), 1.0);
		ch_ofs += 4;
	}

	fill_channel(buf, 84, ppos.game_ply() / 256.0F);
	fill_channel(buf, 85, (float)ppos.in_check());
	return py::array_t<float>(
		py::buffer_info(
			buf,
			sizeof(float),
			py::format_descriptor<float>::format(),
			3,
			{ 86, 9, 9 },
			{ sizeof(float) * 9 * 9,  sizeof(float) * 9,  sizeof(float) }
		)
		);
}

py::array_t<float> DNNConverter::get_move_array(const PyPosition & pos, PyMove move) const
{
	float buf[139 * 9 * 9] = {};
	buf[get_move_index(pos, move)] = 1;
	return py::array_t<float>(
		py::buffer_info(
			buf,
			sizeof(float),
			py::format_descriptor<float>::format(),
			3,
			{ 139, 9, 9 },
			{ sizeof(float) * 9 * 9,  sizeof(float) * 9,  sizeof(float) }
		)
		);
}

py::array_t<float> DNNConverter::get_legal_move_array(const PyPosition & pos) const
{
	float buf[139 * 9 * 9] = {};
	const Position &ppos = pos.pos;
	for (auto m : MoveList<LEGAL>(ppos))
	{
		buf[get_move_index_inner(ppos, m)] = 1;
	}
	return py::array_t<float>(
		py::buffer_info(
			buf,
			sizeof(float),
			py::format_descriptor<float>::format(),
			3,
			{ 139, 9, 9 },
			{ sizeof(float) * 9 * 9,  sizeof(float) * 9,  sizeof(float) }
		)
		);
}

int DNNConverter::get_move_index(const PyPosition & pos, PyMove move) const
{
	return get_move_index_inner(pos.pos, move.m);
}

PyMove DNNConverter::reverse_move_index(const PyPosition & pos, int move_index) const
{
	return PyMove(reverse_move_index_inner(pos.pos, move_index));
}

Move DNNConverter::reverse_move_index_inner(const Position & pos, int move_index) const
{
	int ch = move_index / (int)SQ_NB;
	Square _move_from = (Square)(move_index % (int)SQ_NB);
	Color side_to_move = pos.side_to_move();
	if (ch >= 132)
	{
		// drop
		Piece pt = (Piece)(ch - 132 + 1);
		if (side_to_move == WHITE) {
			_move_from = Inv(_move_from);
		}
		return make_move_drop(pt, _move_from);
	}
	else
	{
		// move
		bool is_promote = ch >= 66;
		if (is_promote)
		{
			ch -= 66;
		}

		int from_file = file_of(_move_from);
		int from_rank = rank_of(_move_from);

		int to_file, to_rank;
		if (ch == 64)
		{
			to_file = from_file - 1;
			to_rank = from_rank - 2;
		}
		else if (ch == 65)
		{
			to_file = from_file + 1;
			to_rank = from_rank - 2;
		}
		else
		{
			int dirs[][2] = { { -1,-1 },{ -1,0 },{ -1,1 },{ 0,-1 },{ 0,1 },{ 1,-1 },{ 1,0 },{ 1,1 } };
			int dir_index = ch / 8;
			int move_length = ch % 8 + 1;
			to_file = from_file + dirs[dir_index][0] * move_length;
			to_rank = from_rank + dirs[dir_index][1] * move_length;
		}
		Square _move_to = (File)to_file | (Rank)to_rank;
		if (side_to_move == WHITE) {
			_move_from = Inv(_move_from);
			_move_to = Inv(_move_to);
		}

		if (is_promote)
		{
			return make_move_promote(_move_from, _move_to);
		}
		else
		{
			return make_move(_move_from, _move_to);
		}
	}
}

int DNNConverter::get_move_index_inner(const Position & pos, Move m) const
{
	/*
	AlphaZeroの論文を参考に作成
	9x9は移動元。
	139次元のうち、64次元は「クイーン」の動き(8方向*最大8マス)。
	2次元は桂馬の動き。66次元は前述の64+2次元と動きは同じで、成る場合。
	7次元は駒を打つ場合で、この場合の座標は打つ先。
	クイーンの動きは(筋,段)=(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)
	* 後手番の際は、盤面・駒の所属を反転して先手番の状態にする。
	*/
	Color side_to_move = pos.side_to_move();
	Square _move_to = move_to(m);
	if (side_to_move == WHITE) {
		_move_to = Inv(_move_to);
	}

	if (is_drop(m))
	{
		return (move_dropped_piece(m) - PAWN + 132) * (int)SQ_NB + _move_to;
	}
	else
	{
		Square _move_from = move_from(m);
		if (side_to_move == WHITE) {
			_move_from = Inv(_move_from);
		}

		int file_diff = file_of(_move_to) - file_of(_move_from);
		int rank_diff = rank_of(_move_to) - rank_of(_move_from);
		int ch;
		if (file_diff == -1 && rank_diff == -2)
		{
			ch = 64;
		}
		else if (file_diff == 1 && rank_diff == -2)
		{
			ch = 65;
		}
		else if (file_diff < 0)
		{
			if (rank_diff < 0)
			{
				ch = -1 + -file_diff;
			}
			else if (rank_diff == 0)
			{
				ch = 7 + -file_diff;
			}
			else
			{
				ch = 15 + -file_diff;
			}
		}
		else if (file_diff == 0)
		{
			if (rank_diff < 0)
			{
				ch = 23 + -rank_diff;
			}
			else
			{
				ch = 31 + rank_diff;
			}
		}
		else
		{
			// fild_diff > 0
			if (rank_diff < 0)
			{
				ch = 39 + file_diff;
			}
			else if (rank_diff == 0)
			{
				ch = 47 + file_diff;
			}
			else
			{
				ch = 55 + file_diff;
			}
		}

		if (is_promote(m))
		{
			ch += 66;
		}

		return ch * (int)SQ_NB + _move_from;
	}
}
