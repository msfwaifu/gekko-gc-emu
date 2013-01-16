/**
 * Copyright (C) 2005-2012 Gekko Emulator
 *
 * @file    bp_mem.cpp
 * @author  ShizZy <shizzy247@gmail.com>
 * @date    2012-03-10
 * @brief   Implementation of BP for the graphics processor
 *
 * @section LICENSE
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * Official project repository can be found at:
 * http://code.google.com/p/gekko-gc-emu/
 */

#include "common.h"
#include "hash.h"
#include "memory.h"

#include <GL/glew.h>

#include "hw/hw_pe.h"

#include "renderer_gl3/shader_manager.h"

#include "utils.h"
#include "video_core.h"
#include "vertex_manager.h"
#include "fifo.h"
#include "fifo_player.h"
#include "bp_mem.h"
#include "texture_decoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics Processor namespace

#undef LOG_DEBUG
#define LOG_DEBUG(x,y, ...)

namespace gp {

BPMemory g_bp_regs; ///< BP memory/registers

/// Write a BP register
void BP_RegisterWrite(u8 addr, u32 data) {
    LOG_DEBUG(TGP, "BP_LOAD [%02x] = %08x", addr, data);

	// Write data to bp memory
    g_bp_regs.mem[addr] = data;

    // Write to renderer
	//	Note: This should always happen first to compare what was last written!
    video_core::g_renderer->WriteBP(addr, data);

    // Adjust GX globals accordingly
    switch(addr) {
    case BP_REG_GENMODE: // GEN_MODE
        video_core::g_renderer->SetGenerationMode();
        break;

    case BP_REG_SCISSORTL: // SU_SCIS0 - Scissorbox Top Left Corner
    case BP_REG_SCISSORBR: // SU_SCIS1 - Scissorbox Bottom Right Corner
    case BP_REG_SCISSOROFFSET:
        video_core::g_renderer->SetScissorBox();
        break;

    case BP_REG_LINEPTWIDTH: // SU_LPSIZE - Line and Point Size
        //gx_states::set_lpsize();
        LOG_DEBUG(TGP, "BP-> SU_LPSIZE");
        break;

    case BP_REG_TREF + 0: // RAS_TREF0
    case BP_REG_TREF + 1: // RAS_TREF1
    case BP_REG_TREF + 2: // RAS_TREF2
    case BP_REG_TREF + 3: // RAS_TREF3
    case BP_REG_TREF + 4: // RAS_TREF4
    case BP_REG_TREF + 5: // RAS_TREF5
    case BP_REG_TREF + 6: // RAS_TREF6
    case BP_REG_TREF + 7: // RAS_TREF7
        //gx_tev::set_modifed();
        LOG_DEBUG(TGP, "BP-> RAS_TREFx");
        break;

    case BP_REG_PE_ZMODE: // PE_ZMODE set z mode
        video_core::g_renderer->SetDepthMode();
        break;

    case BP_REG_PE_CMODE0: // PE_CMODE0 dithering / blend mode/color_update/alpha_update/set_dither
        //gx_states::set_cmode0();
        if (data & 0xFFFF) {
            // Set LogicOp Blending Mode
            if (data & 2) {
                video_core::g_renderer->SetLogicOpMode();
            }
            // Set Dithering Mode
            if (data & 4) {
                video_core::g_renderer->SetDitherMode();
            }
            // Set Blending Mode
            if (data & 0xFE1) {
                video_core::g_renderer->SetBlendMode(false);
            }
            // Set Color Mask
            if (data & 0x18) {
                // TODO(ShizZy): Renable when we have EFB emulated
                video_core::g_renderer->SetColorMask();
            }
        }
        break;

    case BP_REG_PE_CMODE1: // PE_CMODE1 destination alpha
        //gx_tev::set_modifed();
        LOG_DEBUG(TGP, "BP-> PE_CMODE1");
        break;

    case BP_REG_PE_CONTROL: // PE_CONTROL comp z location z_comp_loc(0x43000040)pixel_fmt(0x43000041)
        //video_core::g_renderer->SetColorMask(); TODO(ShizZy): Renable when we have EFB emulated
        break;

    case BP_REG_PE_DRAWDONE: // PE_DONE - draw done

	    if (g_bp_regs.mem[0x45] & 0x2) { // enable interrupt
            // Flush vertex buffer
            VertexManager_Flush();

            video_core::g_renderer->SwapBuffers();
            if (fifo_player::IsRecording())
                fifo_player::FrameFinished();
            Fifo_Reset();
            GX_PE_FINISH = 1;
            video_core::g_current_frame++;
            video_core::g_texture_manager->Purge();
        }
        break;

    case BP_REG_PE_TOKEN: // PE_TOKEN
        GX_PE_TOKEN_VALUE = (data & 0xffff);
        break;

    case BP_REG_PE_TOKEN_INT: // PE_TOKEN_INT
        GX_PE_TOKEN_VALUE = (data & 0xffff); 
        GX_PE_TOKEN = 1;
        break;

    case BP_REG_EFB_COPY:  // pe copy execute
        {
            Rect efb_rect;
            efb_rect.x0_ = gp::g_bp_regs.efb_top_left.x;
            efb_rect.y0_ = gp::g_bp_regs.efb_top_left.y;
            efb_rect.x1_ = efb_rect.x0_ + gp::g_bp_regs.efb_height_width.x + 1;
            efb_rect.y1_ = efb_rect.y0_ + gp::g_bp_regs.efb_height_width.y + 1;

            BPEFBCopyExec efb_copy_exec;
            efb_copy_exec._u32 = data;

            if (efb_copy_exec.copy_to_xfb) {
                f32 scale_y;
                if (efb_copy_exec.scale_invert) {
                    scale_y = 256.0f / (f32)gp::g_bp_regs.disp_copy_y_scale;
                } else {
                    scale_y = (f32)gp::g_bp_regs.disp_copy_y_scale / 256.0f;
                }
                u32 xfb_height = (u32)(((f32)gp::g_bp_regs.efb_height_width.y + 1.0f) * scale_y);
                u32 xfb_width = gp::g_bp_regs.disp_stride << 4;
                Rect xfb_rect(0, 0, xfb_width, xfb_height);

                video_core::g_renderer->CopyToXFB(efb_rect, xfb_rect);
            } else {
                video_core::g_texture_manager->CopyEFB(
                    gp::g_bp_regs.efb_copy_addr << 5, 
                    static_cast<BPPixelFormat>(gp::g_bp_regs.zcontrol.pixel_format), 
                    efb_copy_exec,
                    efb_rect
                );
            }
            if (efb_copy_exec.clear) {
                bool enable_color = gp::g_bp_regs.cmode0.color_update;
                bool enable_alpha = gp::g_bp_regs.cmode0.alpha_update;
                bool enable_z = gp::g_bp_regs.zmode.update_enable;

                // Disable unused alpha channels
                if (!g_bp_regs.zcontrol.is_efb_alpha_enabled()) {
                    enable_alpha = false;
                }
                if (enable_color || enable_alpha || enable_z) {
                    u32 color = (g_bp_regs.clear_ar << 16) | g_bp_regs.clear_gb;

                    u32 z = g_bp_regs.clear_z;

		            // Drop additional accuracy
		            if (g_bp_regs.zcontrol.pixel_format == kPixelFormat_RGBA6_Z24) {
			            color = format_precision::rgba8_with_rgba6(color);
		            } else if (g_bp_regs.zcontrol.pixel_format == kPixelFormat_RGB565_Z16) {
			            color = format_precision::rgba8_with_rgb565(color);
			            z = format_precision::z24_with_z16(z);
		            }
                    video_core::g_renderer->Clear(
                        efb_rect,                   // Clear rectangle
                        enable_color,               // Enable color clearing
                        enable_alpha,               // Enable alpha clearing
                        enable_z,                   // Enable depth clearing
                        color,                      // Clear color
                        z);                         // Clear depth
                }
        
            }
            break;
        }

    case BP_REG_LOADTLUT0: // TX_LOADTLUT0
    case BP_REG_LOADTLUT1: // TX_LOADTLUT1
        {
            u32 cnt = ((g_bp_regs.mem[0x65] >> 10) & 0x3ff) << 5;
	        u32 mem_addr = (g_bp_regs.mem[0x64] & 0x1fffff) << 5;
	        u32 tlut_addr = (g_bp_regs.mem[0x65] & 0x3ff) << 5;

	        memcpy(&gp::tmem[tlut_addr & TMEM_MASK], &Mem_RAM[mem_addr & RAM_MASK], cnt);
            LOG_DEBUG(TGP, "BP-> TX_LOADTLUTx");
            break;
        }
        break;
    }
}

/// Load a texture
void BP_LoadTexture() {
    for (int num = 0; num < kGCMaxActiveTextures; num++) {
        int set = (num & 4) >> 2;
        int index = num & 7;
        for (int stage = 0; stage < kGCMaxTevStages; stage++) {
            if (gp::g_bp_regs.tevorder[stage >> 1].get_texmap(stage) == num) {
                video_core::g_texture_manager->UpdateData(num, g_bp_regs.tex[set].image_0[index],
                    g_bp_regs.tex[set].image_3[index]);
                video_core::g_texture_manager->Bind(num);
                video_core::g_texture_manager->UpdateParameters(num, 
                    g_bp_regs.tex[set].mode_0[index], g_bp_regs.tex[set].mode_1[index]);
                break;
            }
        }
    }
}

/// Initialize BP
void BP_Init() {
    memset(&g_bp_regs, 0, sizeof(g_bp_regs));

    // Clear EFB on startup with alpha of 1.0f
    // TODO(ShizZy): Remove hard coded EFB rect size (still need a video_core or renderer interface
    // for this, actually)
    video_core::g_renderer->Clear(
        Rect(0, 0, 640, 480),   // Clear rectangle
        true,                   // Enable color clearing
        true,                   // Enable alpha clearing
        true,                   // Enable depth clearing
        0xFF000000,             // Clear color - ARGB color black with alpha set to 1.0f!
        0);                     // Clear depth
}

} // namespace