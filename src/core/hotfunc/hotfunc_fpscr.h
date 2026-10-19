// The generated code's switch to scalar FP mode (FZ off), for the hand-tuned
// copies that reproduce it. With fixed_flush_mode in fable_2_manifest.toml the
// generated code never leaves flush mode (fable_2_pch.h defines
// REX_FIXED_FLUSH_MODE 1), and neither may these: they would turn FZ off
// under code that no longer turns it back on.
#pragma once

#ifndef REX_FIXED_FLUSH_MODE
#define REX_FIXED_FLUSH_MODE 0
#endif

#if REX_FIXED_FLUSH_MODE
#define HOTFUNC_DISABLE_FLUSH_MODE(ctx) ((void)0)
#else
#define HOTFUNC_DISABLE_FLUSH_MODE(ctx) (ctx).fpscr.disableFlushMode()
#endif
