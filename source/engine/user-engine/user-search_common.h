#pragma once
#include "../../extra/all.h"
#include "../../../ipqueue/ipqueue/ipqueue.h"
#include "dnn_eval_obj.h"

extern ipqueue<dnn_eval_obj> *eval_queue;
extern ipqueue<dnn_result_obj> *result_queue;

void init_dnn_queues();
void show_error_if_dnn_queue_fail();
int dnn_get_move_index(const Position & pos, Move m);
void dnn_write_eval_obj(dnn_eval_obj *eval_obj, const Position &pos);
