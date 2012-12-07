/**
 * Copyright (C) 2005-2012 Gekko Emulator
 *
 * @file    texture_manager.h
 * @author  ShizZy <shizzy247@gmail.com>
 * @date    2012-12-05
 * @brief   Handles detecting new textures and notifying the renderer
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

#ifndef VIDEO_CORE_TEXTURE_MANAGER_H_
#define VIDEO_CORE_TEXTURE_MANAGER_H_

#include "common.h"

namespace gp {

enum TextureType {
    kTextureType_None = 0,  
    kTextureType_Normal,        ///< Texture is raw RAM data
    kTextureType_EFB,           ///< Texture is result of an EFB copy
};

/// Initialize the texture manager
void TextureManagerInit();

} // namespace

#endif // VIDEO_CORE_TEXTURE_MANAGER_H_