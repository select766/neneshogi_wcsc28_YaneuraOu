#include "pyinterface.h"
#include "thread.h"

bool PyPosition::initialized = false;
static MainThread *main_thread = nullptr;

PyPosition::PyPosition()
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
	pos.set(sfen, &state[0], Threads.main());
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

PyMove PyMove::from_usi(const std::string str)
{
	return PyMove(move_from_usi(str));
}

std::string PyMove::to_usi_string()
{
	return ::to_usi_string(m);
}
