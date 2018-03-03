#pragma once
#include "../../position.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

class PyMove {
public:
	static PyMove MOVE_NONE;
	static PyMove MOVE_NULL;
	static PyMove MOVE_RESIGN;
	static PyMove MOVE_WIN;
	Move m;
	PyMove(Move m);
	static PyMove make_move(int32_t from, int32_t to);
	static PyMove make_move_promote(int32_t from, int32_t to);
	static PyMove make_move_drop(uint32_t pt, int32_t to);
	static PyMove from_int(uint16_t move);
	static PyMove from_usi(const std::string str);
	bool is_ok();
	bool is_drop();
	bool is_promote();
	int special();
	int move_from();
	int move_to();
	int move_dropped_piece();
	std::string to_usi_string();
	uint16_t to_int();
	bool __eq__(const PyMove m);
};

class PyPosition {
public:
	StateInfo state[1024];
	Move moves[1024];
	StateInfo init_state;
	Position pos;
	int16_t psv_score;
	PyMove psv_move;
	uint16_t psv_game_ply;
	int8_t psv_game_result;
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
	bool legal(PyMove m) const;
	py::array_t<uint32_t> get_board();
	py::array_t<uint32_t> get_hand();
	std::vector<PyMove> genereate_move_list();
	bool set_from_packed_sfen(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> packed_sfen);
	bool set_from_packed_sfen_value(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> packed_sfen);
};

class DNNConverter {
public:
	int format_board, format_move;
	DNNConverter(int _format_board, int _format_move);
	py::tuple board_shape() const;
	py::tuple move_shape() const;
	py::array_t<float> get_board_array(const PyPosition& pos) const;
	py::array_t<float> get_move_array(const PyPosition& pos, PyMove move) const;
	py::array_t<float> get_legal_move_array(const PyPosition& pos) const;
	int get_move_index(const PyPosition& pos, PyMove move) const;
	PyMove reverse_move_index(const PyPosition& pos, int move_index) const;
private:
	int get_move_index_inner(const Position& pos, Move m) const;
	Move reverse_move_index_inner(const Position& pos, int move_index) const;
};
