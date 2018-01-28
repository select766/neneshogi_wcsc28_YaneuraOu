#include "pyinterface.h"
#include "thread.h"

bool PyPosition::initialized = false;
static MainThread *main_thread = nullptr;

PyPosition::PyPosition() : psv_move(MOVE_NONE)
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
