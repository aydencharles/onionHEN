/* Copyright (C) 2025 OnionHEN / LightningMods */
#pragma once

struct MonoImage;

/*
 * ReactButton uses invertedIconSource while focused.  Asset-path icons only
 * populate iconSource, so mirror OnionHEN's source into both properties.
 * Shared by HomeUI top navigation and the Control Center extension host.
 */
void install_onion_icon_source_hook(MonoImage *react_pui);
