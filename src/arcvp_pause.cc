//
// Created by delta on 12/31/2024.
//


#include "arcvp.h"

bool ArcVP::resume() {
  sendPresentVideoEvent();
  return true;
}
