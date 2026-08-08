#ifndef DIRED_UPDATE_H
#define DIRED_UPDATE_H

#include "model.h"
#include "msg.h"
#include "cmd.h"

void update(const Msg *msg, const Model *model, Model *out_model, Cmd *out_cmd);

#endif // DIRED_UPDATE_H
