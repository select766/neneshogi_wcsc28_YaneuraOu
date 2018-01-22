#pragma once
#include "../../position.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

class PyMove {
public:
	Move m;
	PyMove(Move m);
	static PyMove make_move(int32_t from, int32_t to);
	static PyMove make_move_promote(int32_t from, int32_t to);
	static PyMove make_move_drop(uint32_t pt, int32_t to);
	static PyMove from_usi(const std::string str);
	std::string to_usi_string();
};

class PyPosition {
public:
	StateInfo state[1024];
	Move moves[1024];
	Position pos;
	static bool initialized;

	PyPosition();
	void set(const std::string sfen);
	void set_hirate();
	int side_to_move() const;
	int game_ply() const;
	const std::string sfen() const;
	void do_move(PyMove m);
	void undo_move();
	void do_null_move();
	void undo_null_move();
	uint64_t key() const;
	bool in_check() const;
	bool is_mated() const;
	py::array_t<uint32_t> get_board();
	py::array_t<uint32_t> get_hand();
	std::vector<PyMove> genereate_move_list();
};
