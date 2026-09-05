#pragma once

void cam_init(bool front);
void cam_stop();
void cam_switch();
void cam_dmaStart(int dma, void* dst);
