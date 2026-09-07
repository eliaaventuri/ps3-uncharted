# Graph Report - ps3-uncharted  (2026-09-07)

## Corpus Check
- Large corpus: 539 files · ~931,380 words. Semantic extraction will be expensive (many Claude tokens). Consider running on a subfolder.

## Summary
- 7279 nodes · 24867 edges · 287 communities (253 shown, 20 thin omitted)
- Extraction: 67% EXTRACTED · 33% INFERRED · 0% AMBIGUOUS · INFERRED: 8293 edges (avg confidence: 0.85)
- Token cost: 426,806 input · 75,317 output

## Community Hubs (Navigation)
- Lifted SPU: libsre kernel
- Lifted SPU: libsre kernel2
- Lifted SPU: mixed images
- Lifted SPU: mixed images 2
- Lifted SPU: CRI job
- libgcm / CellGcm graphics system
- PPU thread context (ppu_context)
- stb_image decoder (vendored)
- RSX live-draw + D3D12 runtime config
- RSX state struct + dirty flags
- RSX D3D12 backend
- cellJpgEnc HLE module
- PPU lifter test harness
- PS3 error codes + HLE headers
- Lifter torture suite (C)
- RSX texture decode (D3D12)
- RPCS3 GDB-stub oracle probes
- sysPrxForUser (PS3 stdlib)
- tools/compare report + ingest
- PPU loader + guest-stack debug
- HLE ABI audit tool
- find_functions.py (function boundary finder)
- cellFontFT HLE module
- stb_image
- stb_truetype
- RSX command dispatch
- PPU lifter core (Instruction/PPULifter)
- cellPad HLE module
- ELF parser (pyelftools-based)
- ppu_fs
- CI workflows + boot-scaffold gap
- cellSail
- Lifted SPU: lifted
- stb_truetype
- cellSpurs
- NID database resolver
- cellSync
- cellGcm HLE module
- PPU conformance harness
- main
- RSX fragment-program decompiler
- SPU MFC DMA channels
- SPU fallback dispatch (lv2_register)
- cellSync2
- rsx_commands
- SPU context + channels
- cellfs HLE module
- cellFont
- cellRtc
- cellNet HLE module
- unfself
- main
- Endian byte-swap primitives (endian.h)
- spu_recomp_register entry points
- rsx_null_backend
- ps3_recomp_harness
- post_lift.py + ppu_lifter.py
- stb_image
- sceNpTrophy
- PS3 module / NID table loader
- stb_truetype
- cellHttp
- cellPamf
- win32_compat threading shim
- NID system concepts (SHA-1, DECLARE_PS3_MODULE)
- spu_workload
- gen_vectors
- cellAudio HLE module
- rsx_live_draw
- win32_backtrace + boot_main
- RSX backend headers
- test_texture_layout
- gen_test_torture
- disasm_audit_operands
- Lifted SPU: cri_job_lifted
- find_spu_functions
- cellAdecAtrac3p HLE module
- cellCamera HLE module
- cellJpgDec HLE module
- Xbox360-vs-PS3 cross-recompilation candidates
- stb_image
- rsx_vertex_compact
- test_prx_loader
- sys_fs
- cellGifDec HLE module
- cellSpursJq HLE module
- nid_harvest
- stb_truetype
- stb_truetype
- stb_truetype
- sceNp
- LV2 syscall dispatch table
- sys_event
- cellDmux HLE module
- cellResc HLE module
- cellSaveData
- cellL10n
- sceNpMatching2 HLE module
- rsx_vp_decompiler
- Guest virtual memory (vm.h)
- ELF file loader (ELFFile)
- ppu_disasm
- cellSpurs
- sceNpTus HLE module
- dwarf_common
- cellSysutil HLE module
- SPU_LIFTER doc: next priorities
- be_t big-endian wrapper type
- sceNpBasic HLE module
- cellGame HLE module
- cellNetCtl HLE module
- cellOskDialog HLE module
- cellVoice HLE module
- cellFiber HLE module
- cellGem HLE module
- sys_ppu_thread syscall
- SPU lockstep atomics
- cellMic
- cellPngDec HLE module
- sceNpCommerce2 HLE module
- lift_audit.py baseline metrics
- gen_vectors
- disasm_audit
- cellMouse HLE module
- cellRudp HLE module
- main
- test_spu_helpers
- audit_guest_ptrs
- spu_disasm_audit_operands
- cellUsbd HLE module
- cellVideoOut HLE module
- sceNpSignaling HLE module
- rsx_commands
- test_adapter_main
- spu_lifter.py (SPU decode+lift)
- cellImeJp HLE module
- cellSaveData
- cellSpurs
- cellVdec HLE module
- stb_image
- host_posix
- sys_semaphore
- SPU interpreter fallback
- test_taskset_main
- dwarf_abi
- endian_audit
- wrap_spu_elf
- cellHttpUtil HLE module
- gen_vectors
- xex_parser
- spu_lifter.py branch analysis
- cellAdec HLE module
- cellKb HLE module
- ExportCompareUnits
- sceNpClans
- test_fp_msl
- gen_vectors
- pkg_extract
- ppu_loader.py (OPD/imports)
- spu_ident
- cellDaisy
- elf_symbols.py + recall bug
- NID SHA-1 computation (nid.h)
- PPU guest-call runtime (ppu_run)
- sceNpUtil HLE module
- test_event_main
- test_lfqueue_main
- lift_prx
- spu_disasm.py (SPU decoder)
- cellAvconfExt
- cellMsgDialog HLE module
- cellSsl
- SYSCALLS doc: platform switches
- LBP CMake build graph
- sceNpCommerce HLE module
- yz_frontier_trace
- elf_parser
- cellBGDL
- cellHttps
- cellMusicDecode2
- cellSheap HLE module
- cellSubdisplay HLE module
- cellUserInfoSelectUser HLE module
- spurs_taskset
- spu_disasm_audit
- cellAdecCelp8 HLE module
- cellVdecDivx HLE module
- test_torture_main
- sceNpSns HLE module
- sys_interrupt
- compare harness README
- cellMusicDecode
- cellOvis HLE module
- cellFsUtility HLE module
- build_spu_workloads
- check_ppu_scaffold.py ratchet
- parse_rrc
- cellFreeType HLE module
- cellKey2char
- cellVideoUpload HLE module
- cellSysmodule
- spu_drain
- test_brsl_return_main
- test_shufb_main
- test_sum_main
- test_workload_main
- run_torture
- ghidra_names.py address map
- cellGameExec HLE module
- host_main.c guest invoke
- cellSpurs
- guest-pointer fault trap
- test_dma_main
- gen_vectors
- extract_jobmods
- find_indirect_entries.py (session 6)
- generate_stubs.py (HLE stub gen)
- gen_test_workload
- disasm operand-divergence whitelist
- cellPhotoExport
- cellScreenshot
- cellSpurs
- cellRemotePlay
- requirements
- gen_test_adapter
- gen_test_bringup
- gen_test_dma
- gen_test_event
- gen_test_shufb
- gen_test_sum
- ida_export_units
- extract_spu_images
- spu_lift_selftest
- cellPhotoImport
- cellPrint
- icon_1024
- gen_test_brsl_return
- gen_test_lfqueue
- gen_test_taskset
- rebase_spu_elf
- spbalance
- RSX_FRAGMENT_PROGRAM
- cellLicenseArea
- spu_recomp
- gen_vectors
- callgraph
- gen_spu_workloads
- inject_spcheck
- ppu_loader
- spu_recomp
- spu_recomp
- main
- gen_spu_interp
- ghidra_analyze
- test_boot_posix
- DEBUG_CONSOLE
- MODULES_REFERENCE
- spu_xswd_test
- CMakeLists
- ida_analyze
- defrag_func0
- make_test_elf
- triage_elf
- ARCHITECTURE
- NID_SYSTEM
- make_dmg
- run_tests
- stderr_revert2
- MODULES_REFERENCE
- MODULES_REFERENCE
- PLATFORM_ABSTRACTION
- RSX_GRAPHICS
- hle_abi_audit_whitelist

## God Nodes (most connected - your core abstractions)
1. `spu_context` - 973 edges
2. `spu_ls_read128()` - 448 edges
3. `spu_ls_write128()` - 411 edges
4. `spu_ai()` - 402 edges
5. `spu_il()` - 386 edges
6. `spu_ori()` - 324 edges
7. `spu_selb()` - 291 edges
8. `spu_a()` - 249 edges
9. `ppu_context` - 221 edges
10. `spu_rotqbyi()` - 185 edges

## Surprising Connections (you probably didn't know these)
- `spu_recomp_register()` --calls--> `spu_register_function()`  [INFERRED]
  fw_spu/spu_libsre/lifted/spu_recomp.c → runtime/spu/spu_channels.c
- `cellSpursAttachLv2EventQueue()` --calls--> `sys_event_queue_uncancel_by_id()`  [INFERRED]
  libs/spurs/cellSpurs.c → runtime/syscalls/sys_event.c
- `null_end_frame()` --calls--> `GetTickCount64()`  [INFERRED]
  libs/video/rsx_null_backend.c → runtime/platform/win32_compat.h
- `HLE Module Anatomy` --semantically_similar_to--> `Contributing Guide`  [INFERRED] [semantically similar]
  docs/CUSTOM_MODULES.md → CONTRIBUTING.md
- `MyGameRecomp project template` --semantically_similar_to--> `lbp_ps3 recompiled executable`  [INFERRED] [semantically similar]
  templates/project/CMakeLists.txt → lbp/CMakeLists.txt

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **PPU Boot Scaffold POSIX Port Gap** — readme_ppu_boot_scaffold, _github_workflows_linux_job, _github_workflows_macos_job, docs_building_check_ppu_scaffold, cmakelists_ppu_scaffold_exclusion [EXTRACTED 0.90]
- **Static Recompilation Pipeline Flow** — readme_static_recompilation, docs_architecture_recompilation_pipeline, docs_getting_started_tool_pipeline, docs_game_porting_guide [EXTRACTED 0.85]
- **Guest-to-HLE Call Bridging** — docs_architecture_nid_system, docs_custom_modules_calling_convention_bridge, docs_game_porting_guide_hle_bridge_pattern, docs_game_porting_guide_toc_save [EXTRACTED 0.85]
- **PPU static recompilation pipeline** — tools_elf_parser, docs_tools_ppu_disasm, docs_tools_ppu_lifter, docs_ppu_recomp_ppu_loader, docs_ppu_recomp_nid_hle_bridge [EXTRACTED 1.00]
- **SPU image to native execution pipeline** — docs_spu_lifter_extract_spu_images, docs_spu_lifter_find_spu_functions, docs_spu_lifter_spu_lifter_tool, docs_spu_lifter_spu_channels_runtime [EXTRACTED 1.00]
- **RSX three-layer graphics translation** — docs_rsx_graphics_cellgcmsys_hle, docs_rsx_graphics_rsx_command_processor, docs_rsx_graphics_rsx_backend_interface, docs_rsx_graphics_d3d12_backend [EXTRACTED 1.00]
- **Cross-platform 360/PS3 comparison pipeline** — tools_compare_readme_unified_unit_schema, tools_compare_readme_match_functions_py, tools_compare_readme_ingest_native_py, tools_compare_readme_xenonrecomp_rexglue [EXTRACTED 0.90]
- **ps3recomp harness triage tier tools** — tools_harness_readme_tiered_triage, tools_elf_parser, tools_find_functions, tools_ppu_lifter, tools_harness_readme_ps3sce [EXTRACTED 0.85]
- **SPU operand-audit derivation triad** — tools_spu_disasm_audit_operands_whitelist_spu_disasm_py, tools_spu_lifter, runtime_spu_spu_helpers [EXTRACTED 0.85]

## Communities (287 total, 20 thin omitted)

### Community 0 - "Lifted SPU: libsre kernel"
Cohesion: 0.05
Nodes (224): spu_func_000000D0(), spu_func_000001B8(), spu_func_00000540(), spu_func_00000558(), spu_func_00002DB8(), spu_func_00005180(), spu_func_000051E0(), spu_func_000052E4() (+216 more)

### Community 1 - "Lifted SPU: libsre kernel2"
Cohesion: 0.04
Nodes (196): spu_func_00005180(), spu_func_000051E0(), spu_func_00008090(), spu_func_00008160(), spu_func_00008438(), spu_func_00008848(), spu_func_000090A0(), spu_func_00000290() (+188 more)

### Community 2 - "Lifted SPU: mixed images"
Cohesion: 0.05
Nodes (197): spu_func_00000F58(), spu_func_000013C0(), spu_func_00002DC8(), spu_func_000056A0(), spu_func_000081B0(), spu_func_000083CC(), spu_func_00008760(), spu_func_00008890() (+189 more)

### Community 3 - "Lifted SPU: mixed images 2"
Cohesion: 0.04
Nodes (165): spu_func_00000E28(), spu_func_00005348(), spu_func_000055B0(), spu_func_00005748(), spu_func_000058C8(), spu_func_000059B8(), spu_func_00007388(), spu_func_00007420() (+157 more)

### Community 4 - "Lifted SPU: CRI job"
Cohesion: 0.07
Nodes (152): spu_func_00000088(), spu_func_00000158(), spu_func_00000268(), spu_func_00000270(), spu_func_000002D8(), spu_func_000003E8(), spu_func_000003F8(), spu_func_00000500() (+144 more)

### Community 5 - "libgcm / CellGcm graphics system"
Cohesion: 0.04
Nodes (113): CellGcmConfig, CellGcmControl, CellGcmDisplayInfo, CellGcmFlipHandler, CellGcmGraphicsHandler, CellGcmGuestAlloc, CellGcmGuestWrite32, CellGcmOffsetTable (+105 more)

### Community 6 - "PPU thread context (ppu_context)"
Cohesion: 0.04
Nodes (98): vm_commit(), ppu_context, cia, cr, ctr, fpr, fpscr, gpr (+90 more)

### Community 7 - "stb_image decoder (vendored)"
Cohesion: 0.05
Nodes (95): FILE, wchar_t, load_jpeg_image(), resample_row_1(), stbi__blinn_8x8(), stbi__clamp(), stbi__cleanup_jpeg(), stbi__compute_y() (+87 more)

### Community 8 - "RSX live-draw + D3D12 runtime config"
Cohesion: 0.06
Nodes (82): D3D12_AUTO_BREADCRUMB_OP, D3D12_MESSAGE_SEVERITY, D3D12_SAMPLER_DESC, D3D12_TEXTURE_ADDRESS_MODE, HRESULT, ld_flush_reason, D3D12_CPU_DESCRIPTOR_HANDLE, LONG (+74 more)

### Community 9 - "RSX state struct + dirty flags"
Cohesion: 0.02
Nodes (88): u8, rsx_state, alpha_dirty, alpha_func, alpha_ref, alpha_test_enable, begin_epoch, blend_color (+80 more)

### Community 10 - "RSX D3D12 backend"
Cohesion: 0.06
Nodes (83): BasicVertex, D3D12_PRIMITIVE_TOPOLOGY, D3D12_RESOURCE_STATES, D3D12DrawRecord, cellGcmResolveLocated(), cellGcmResolveOffset(), gcm_2d_method(), nv3089_blit() (+75 more)

### Community 11 - "cellJpgEnc HLE module"
Cohesion: 0.06
Nodes (73): CellJpgEncHandle, CellJpgEncOutputInfo, CellJpgEncParam, CellPngEncHandle, CellPngEncOutputInfo, CellPngEncParam, s32, u32 (+65 more)

### Community 12 - "PPU lifter test harness"
Cohesion: 0.06
Nodes (76): be_f(), be_h(), be_w(), build_cases(), build_vcases(), case(), cmp_form(), cmpi_form() (+68 more)

### Community 13 - "PS3 error codes + HLE headers"
Cohesion: 0.05
Nodes (18): storage_helper, storage_helper<U, true>, s32, cellSysutilNpEulaCheck(), ppu_cr_get(), ppu_cr_set_s32(), ppu_cr_set_s64(), ppu_cr_set_u32() (+10 more)

### Community 14 - "Lifter torture suite (C)"
Cohesion: 0.08
Nodes (60): binop, i64, SBig, SSmall, apply(), do_longjmp(), fbits_run(), fconv_run() (+52 more)

### Community 15 - "RSX texture decode (D3D12)"
Cohesion: 0.06
Nodes (67): D3D12_GPU_DESCRIPTOR_HANDLE, ld_compact_expand_result, ld_reject_class, DXGI_FORMAT, ID3D12Resource, rsx_dsp_texture, rsx_dsp_vertex_texture, u32 (+59 more)

### Community 16 - "RPCS3 GDB-stub oracle probes"
Cohesion: 0.06
Nodes (27): pcof(), diag.py - learn RPCS3's GDB stub behavior step-by-step (defensive). RPCS3's GDB…, walk(), Oracle probe: capture func_006CEAA0 calls on real-HW RPCS3 to resolve the flОw…, capture(), launch_rpcs3(), main(), _pc() (+19 more)

### Community 17 - "sysPrxForUser (PS3 stdlib)"
Cohesion: 0.08
Nodes (61): DWORD, LPVOID, s32, u32, u64, va_list, console_getc(), console_putc() (+53 more)

### Community 18 - "tools/compare report + ingest"
Cohesion: 0.08
Nodes (45): Any, build_findings(), _corpus_hist(), main(), _normalized_deltas(), Counter, Turn a MatchResult into a human-readable report plus machine-readable findings.…, Compare per-mnemonic frequency between the two corpora as a fraction of total… (+37 more)

### Community 19 - "PPU loader + guest-stack debug"
Cohesion: 0.09
Nodes (52): h, GetModuleHandleA(), RtlCaptureStackBackTrace(), YieldProcessor(), cellFsRead(), ps3_hle_call(), barrier_watch_hit(), EXCEPTION_POINTERS (+44 more)

### Community 20 - "HLE ABI audit tool"
Cohesion: 0.06
Nodes (55): Pattern, body_looks_like_status_return(), build_report(), classify_return(), diff_signatures(), extract_oracle(), extract_ours(), find_matching_brace() (+47 more)

### Community 21 - "find_functions.py (function boundary finder)"
Cohesion: 0.06
Nodes (40): _bl_target(), collect_opd_seeds(), Function, FunctionFinder, _is_bl(), _is_blr(), _is_load_lr(), _is_mflr_r0() (+32 more)

### Community 22 - "cellFontFT HLE module"
Cohesion: 0.07
Nodes (49): CellFontFT, CellFontFTConfig, CellFontFTFontMetrics, CellFontFTGlyphImage, CellFontFTGlyphMetrics, CellFontFTLibrary, CellGameRecordingParam, CellMusicExportCallback (+41 more)

### Community 23 - "stb_image"
Cohesion: 0.10
Nodes (56): stbi__bmp_info(), stbi__bmp_parse_header(), stbi__bmp_set_mask_defaults(), stbi__bmp_test(), stbi__bmp_test_raw(), stbi__get16le(), stbi__get32be(), stbi__get32le() (+48 more)

### Community 24 - "stb_truetype"
Cohesion: 0.11
Nodes (54): equal(), main(), my_stbtt_initfont(), my_stbtt_print(), stbtt_BakeFontBitmap(), stbtt_BakeFontBitmap_internal(), stbtt__compute_crossings_x(), stbtt__cuberoot() (+46 more)

### Community 25 - "RSX command dispatch"
Cohesion: 0.10
Nodes (51): rsx_dispatch, rsx_dsp_texture, rsx_dsp_vertex_texture, u32, u8, dma_to_location(), rsx_dispatch, u32 (+43 more)

### Community 26 - "PPU lifter core (Instruction/PPULifter)"
Cohesion: 0.06
Nodes (33): Instruction, A single decoded PPU instruction., LiftedFunction, _parse_operands(), PPULifter, Generate a C condition expression for a conditional branch., Generate tail-entry functions for branch targets that land inside existing…, If a lifted tail fell off its (possibly capped) end, promote the continuation… (+25 more)

### Community 27 - "cellPad HLE module"
Cohesion: 0.06
Nodes (51): s32, u32, u8, cellPad_poll(), CellPadActParam, motor, CellPadCapabilityInfo, info (+43 more)

### Community 28 - "ELF parser (pyelftools-based)"
Cohesion: 0.05
Nodes (23): Python deps (pyelftools, capstone, pycryptodome), BinaryReader, ELFHeader, main(), PRXExportEntry, PRXImportEntry, Thin wrapper around a bytes buffer with endian-aware reads., SCE SELF header – wraps an encrypted/signed ELF. (+15 more)

### Community 29 - "ppu_fs"
Cohesion: 0.08
Nodes (44): u32, u8, main(), rd_le(), sca_name(), vec_name(), closedir(), DIR (+36 more)

### Community 30 - "CI workflows + boot-scaffold gap"
Cohesion: 0.06
Nodes (47): Linux Headless End-to-End Check, Linux CI Build Job, POSIX Undefined-Symbol Guard, macOS CI Build Job, Headless Metal End-to-End Check, POSIX Boot Test, Release SDK Bundle, Windows CI Build Job (+39 more)

### Community 31 - "cellSail"
Cohesion: 0.13
Nodes (46): CellSailDescriptorHandle, CellSailMemAllocator, CellSailMemAllocatorFuncs, CellSailPlayerAttribute, CellSailPlayerCallback, CellSailPlayerHandle, CellSailPlayerResource, CellSailStreamInfo (+38 more)

### Community 32 - "Lifted SPU: lifted"
Cohesion: 0.11
Nodes (43): spu_func_00000000(), spu_func_00000110(), spu_func_00000140(), spu_func_00000220(), spu_func_00000260(), spu_func_000002A0(), spu_func_00000830(), spu_func_00000898() (+35 more)

### Community 33 - "stb_truetype"
Cohesion: 0.06
Nodes (47): stbtt__close_shape(), stbtt_fontinfo, cff, charstrings, data, fdselect, fontdicts, fontstart (+39 more)

### Community 34 - "cellSpurs"
Cohesion: 0.12
Nodes (45): CellSpursAttribute, CellSpursTasksetAttribute, s32, u64, cellSpursAttributeEnableSpuPrintfIfAvailable(), cellSpursAttributeInitialize(), cellSpursAttributeSetNamePrefix(), cellSpursAttributeSetSpuThreadGroupType() (+37 more)

### Community 35 - "NID database resolver"
Cohesion: 0.06
Nodes (26): get_default_db(), NIDDatabase, In-memory NID database supporting lookup by NID or name., Add an entry. If *nid* is None it will be computed., Populate with the curated built-in list, then auto-augment with every function…, Add every exported (cell*/sys*/sce*) function DEFINED in the repo's libs/ and…, Load entries from a JSON file. Returns count loaded. Expected format: {…, Save the database as JSON (grouped by module). (+18 more)

### Community 36 - "cellSync"
Cohesion: 0.13
Nodes (44): atomic_uint, CellSyncBarrier, CellSyncLFQueue, CellSyncMutex, CellSyncQueue, CellSyncRwm, s32, u16 (+36 more)

### Community 37 - "cellGcm HLE module"
Cohesion: 0.07
Nodes (37): BOOL, LARGE_INTEGER, lbp_hle_complete_pending(), cellGcm_fifo_kick_event(), cellGcm_ref_on_poll(), CreateEventA(), HANDLE, ps3__mono_ns() (+29 more)

### Community 38 - "PPU conformance harness"
Cohesion: 0.07
Nodes (27): spu_clz32(), __builtin_clz(), check_ca(), check_reg(), main(), ppc_mask64(), ppc_mulhd(), ppc_mulhdu() (+19 more)

### Community 39 - "main"
Cohesion: 0.18
Nodes (40): cellGcmSetWaitFlip(), InterlockedIncrement(), Sleep(), alloc_tid(), call1(), call2(), call3(), call4() (+32 more)

### Community 40 - "RSX fragment-program decompiler"
Cohesion: 0.13
Nodes (42): Out, u16, u32, u64, u8, decode_src(), dest_mask(), emit_src() (+34 more)

### Community 41 - "SPU MFC DMA channels"
Cohesion: 0.08
Nodes (39): mfc_for(), mfc_channel_read(), mfc_channel_write(), mfc_cmd, active, cmd, ea, lsa (+31 more)

### Community 42 - "SPU fallback dispatch (lv2_register)"
Cohesion: 0.10
Nodes (41): spu_alloc_group(), spu_alloc_thread(), spu_dispatch_frame_by_queue(), spu_fallback_thread_proc(), spu_find_group(), spu_find_thread(), spu_interp_fallback(), spu_load_image_to_ls() (+33 more)

### Community 43 - "cellSync2"
Cohesion: 0.11
Nodes (41): CellSync2Cond, CellSync2CondAttribute, CellSync2Mutex, CellSync2MutexAttribute, CellSync2Queue, CellSync2QueueAttribute, CellSync2Semaphore, CellSync2SemaphoreAttribute (+33 more)

### Community 44 - "rsx_commands"
Cohesion: 0.09
Nodes (40): vblank_ticker(), cellGcm_take_flip_pending_synced(), cellGcmTickFlip(), cellGcmTickVBlank(), ps3_ms_now(), rsx_reset_upload_claims(), u32, process_surface_method() (+32 more)

### Community 45 - "SPU context + channels"
Cohesion: 0.08
Nodes (36): spu_run_with_halt(), u128, spu_channel_has_data(), spu_channel_read(), spu_channel_write(), spu_context_init(), spu_ls_ptr(), spu_ls_read32() (+28 more)

### Community 46 - "cellfs HLE module"
Cohesion: 0.14
Nodes (39): CellFsDir, CellFsDirent, CellFsFd, CellFsStat, HOST_STAT_T, alloc_dir(), alloc_fd(), s32 (+31 more)

### Community 47 - "cellFont"
Cohesion: 0.15
Nodes (39): CellFont, CellFontConfig, CellFontGlyphImage, CellFontGlyphMetrics, CellFontHorizontalLayout, CellFontLibrary, CellFontRenderer, CellFontRenderSurface (+31 more)

### Community 48 - "cellRtc"
Cohesion: 0.19
Nodes (39): CellRtcDateTime, CellRtcTick, s32, s64, u32, u64, cellRtcConvertLocalTimeToUtc(), cellRtcConvertUtcToLocalTime() (+31 more)

### Community 49 - "cellNet HLE module"
Cohesion: 0.11
Nodes (31): s32, u32, cellNetFinalize(), cellNetInitialize(), cellNetResolverCreate(), cellNetResolverDestroy(), cellNetResolverPollDNS(), cellNetResolverStartAsynDNS() (+23 more)

### Community 50 - "unfself"
Cohesion: 0.09
Nodes (30): Debug fSELF as ground truth, build_elf(), build_fself(), main(), Self-check for unfself.py + elf_symbols.py. No game binary needed: build a tiny…, A minimal ELF64-BE with one exec PT_LOAD and a .symtab naming one function., Wrap `elf` as a compressed debug fSELF (key_revision 0x8000)., classify() (+22 more)

### Community 51 - "main"
Cohesion: 0.10
Nodes (34): hle_ctx_fn, cellSaveData_register_ctx_handlers(), ppu_fn, PS3_THREAD_FN, CreateThread(), DWORD, LPVOID, ppu_fs_register() (+26 more)

### Community 52 - "Endian byte-swap primitives (endian.h)"
Cohesion: 0.11
Nodes (37): be_to_host(), byte_swap(), T, host_to_be(), ps3_bswap128(), ps3_bswap16(), ps3_bswap32(), ps3_bswap64() (+29 more)

### Community 53 - "spu_recomp_register entry points"
Cohesion: 0.07
Nodes (33): spu_recomp_register(), spu_recomp_register(), spu_recomp_register(), spu_recomp_register(), spu_recomp_register(), spu_recomp_register(), spu_recomp_register(), spu_recomp_register() (+25 more)

### Community 54 - "rsx_null_backend"
Cohesion: 0.09
Nodes (30): HWND, LPARAM, LRESULT, u32, u8, UINT, WPARAM, colour_of() (+22 more)

### Community 55 - "ps3_recomp_harness"
Cohesion: 0.13
Nodes (35): Path, build_cfg(), cmd_analyze(), cmd_catalog(), cmd_report(), discover_binaries(), game_label(), log() (+27 more)

### Community 56 - "post_lift.py + ppu_lifter.py"
Cohesion: 0.08
Nodes (34): post_lift.py, ppu_recomp_register() generated entry point, ps3recomp project template guide, detect_functions(), disassemble_bytes(), main(), Disassemble a block of bytes into instructions., Detect function boundaries using prologue/epilogue patterns. Looks for standard… (+26 more)

### Community 57 - "stb_image"
Cohesion: 0.17
Nodes (35): stbi__addints_valid(), stbi__at_eof(), stbi__build_fast_ac(), stbi__build_huffman(), stbi__check_png_header(), stbi__decode_jpeg_header(), stbi__decode_jpeg_image(), stbi__err() (+27 more)

### Community 58 - "sceNpTrophy"
Cohesion: 0.14
Nodes (33): s32, u32, u64, sceNpTrophyCreateContext(), sceNpTrophyCreateHandle(), sceNpTrophyDestroyContext(), sceNpTrophyDestroyHandle(), sceNpTrophyGetGameInfo() (+25 more)

### Community 59 - "PS3 module / NID table loader"
Cohesion: 0.08
Nodes (31): ps3_nid_entry, ps3_nid_table, ps3_find_module(), ps3_module, func_storage, func_table, ps3_module_init(), ps3_module_load() (+23 more)

### Community 60 - "stb_truetype"
Cohesion: 0.09
Nodes (33): stbtt__active_edge, next, stbtt__add_point(), stbtt__edge, invert, x0, x1, y0 (+25 more)

### Community 61 - "cellHttp"
Cohesion: 0.17
Nodes (31): CellHttpClientId, CellHttpTransId, CellHttpUri, http_socket_t, HttpTransSlot, s32, u32, u64 (+23 more)

### Community 62 - "cellPamf"
Cohesion: 0.23
Nodes (31): CellPamfEp, CellPamfReader, audio_channels(), audio_sample_rate(), be16(), be32(), be64(), s32 (+23 more)

### Community 63 - "win32_compat threading shim"
Cohesion: 0.11
Nodes (28): CONDITION_VARIABLE, cellGcm_rsx_process_fifo(), pthread_cond_t, AcquireSRWLockExclusive(), pthread_mutex_t, ps3_cv(), ps3_lazy_obj(), ps3_srw() (+20 more)

### Community 64 - "NID system concepts (SHA-1, DECLARE_PS3_MODULE)"
Cohesion: 0.07
Nodes (32): Module Status Legend (Stubbed/Partial/Complete), cellGcmSys (RSX graphics system), HLE Module structural pattern, sysPrxForUser (PS3 standard library), Custom NID Suffixes (library versioning), DECLARE_PS3_MODULE macro, Module Registration Framework, NID (Name Identifier) (+24 more)

### Community 65 - "spu_workload"
Cohesion: 0.11
Nodes (31): jc_run_one_job(), spurs_ef_set_locked(), spurs_resolve_pm(), spu_ef_deliver_owed(), spu_spurs_taskset_syscall(), spu_lifted_entry_fn, rd_be16(), rd_be32() (+23 more)

### Community 66 - "gen_vectors"
Cohesion: 0.08
Nodes (15): add_carry(), fp_fabs(), fp_fnabs(), fp_fneg(), op_add(), op_addc(), op_adde(), op_addme() (+7 more)

### Community 67 - "cellAudio HLE module"
Cohesion: 0.13
Nodes (29): CellAudioPortConfig, CellAudioPortParam, audio_backend_init(), audio_backend_queued_samples(), audio_backend_shutdown(), audio_backend_submit(), audio_mix_one_block(), audio_mix_thread_func() (+21 more)

### Community 68 - "rsx_live_draw"
Cohesion: 0.11
Nodes (31): D3D12_BLEND, D3D12_BLEND_OP, D3D12_COMPARISON_FUNC, D3D12_GRAPHICS_PIPELINE_STATE_DESC, D3D12_STENCIL_OP, ID3DBlob, apply_render_state(), build_pso() (+23 more)

### Community 69 - "win32_backtrace + boot_main"
Cohesion: 0.12
Nodes (28): cellGcm_flip_request_count(), cellGcm_take_flip_pending(), LPCSTR, GetModuleHandleExA(), HMODULE, ps3__image_base_of(), DWORD, EXCEPTION_POINTERS (+20 more)

### Community 70 - "RSX backend headers"
Cohesion: 0.11
Nodes (19): u32, rsx_primitive_needs_expansion(), rsx_primitive_topology(), u32, rsx_texture_bpp(), rsx_texture_get_format(), rsx_texture_is_swizzled(), rsx_to_dxgi_texture_format() (+11 more)

### Community 71 - "test_texture_layout"
Cohesion: 0.18
Nodes (29): nullsw_bind_texture(), u32, u8, rsx_log2_ceil(), rsx_swizzle_offset(), rsx_texture_argb_is_rgba(), rsx_texture_component_remap(), rsx_texture_decode() (+21 more)

### Community 72 - "gen_test_torture"
Cohesion: 0.12
Nodes (27): ai(), bi(), br(), brhnz(), brhz(), brnz(), brsl(), brz() (+19 more)

### Community 73 - "disasm_audit_operands"
Cohesion: 0.11
Nodes (30): _abs_branch_target_equiv(), canon_operand(), canon_operand_list(), _capstone_dropped_zero_branch_target(), _capstone_or_die(), decode_ours(), executable_words(), load_whitelist() (+22 more)

### Community 74 - "Lifted SPU: cri_job_lifted"
Cohesion: 0.10
Nodes (30): spu_func_00000000(), spu_func_00000018(), spu_func_00000030(), spu_func_00000080(), spu_func_00000150(), spu_func_000071D0(), spu_func_00008020(), spu_func_00008FB8() (+22 more)

### Community 75 - "find_spu_functions"
Cohesion: 0.10
Nodes (29): be_u16(), be_u32(), collect_branch_targets(), collect_brsl_targets(), collect_computed_branch_targets(), collect_duff_targets(), collect_function_pointer_tables(), collect_ila_continuation_targets() (+21 more)

### Community 76 - "cellAdecAtrac3p HLE module"
Cohesion: 0.12
Nodes (24): CellAdecAtrac3pConfig, CellAdecAtrac3pHandle, CellAdecAtrac3pPcmInfo, CellVpostCfgParam, CellVpostCtrlParam, CellVpostHandle, CellVpostPictureInfo, CellVpostResource (+16 more)

### Community 77 - "cellCamera HLE module"
Cohesion: 0.14
Nodes (27): CellCameraCallback, CellCameraInfo, CellCameraReadInfo, s32, u32, cam_fill_frame(), cam_present(), cellCameraClose() (+19 more)

### Community 78 - "cellJpgDec HLE module"
Cohesion: 0.14
Nodes (27): CellJpgDecDataInfo, CellJpgDecInfo, CellJpgDecInParam, CellJpgDecMainHandle, CellJpgDecOpnInfo, CellJpgDecOutParam, CellJpgDecSrc, CellJpgDecSubHandle (+19 more)

### Community 79 - "Xbox360-vs-PS3 cross-recompilation candidates"
Cohesion: 0.08
Nodes (29): tools/compare harness (run_compare.py, xex_parser.py), Cross-recompilation (Xbox 360 vs PS3 diffing), Hedgehog Engine (Sonic Unleashed/Generations), rexglue (portable AOT C++ from Xenia), Candidate selection criteria, XenonRecomp / Unleashed Recompiled, Virtual Memory (VirtualAlloc / mmap MAP_NORESERVE), Lifter ABI (func_XXXX(ppu_context*)) (+21 more)

### Community 80 - "stb_image"
Cohesion: 0.14
Nodes (29): stbi__addsizes_valid(), stbi__bitcount(), stbi__bmp_load(), stbi__compute_transparency(), stbi__compute_transparency16(), stbi__convert_format(), stbi__create_png_alpha_expand8(), stbi__create_png_image() (+21 more)

### Community 81 - "rsx_vertex_compact"
Cohesion: 0.13
Nodes (27): dc_vertex_attr(), dc_vertex_attr_canonical(), ld_vertex_compact_payload(), live_decoded_vertex_hash(), rsx_vertex_layout_plan, u32, u64, u8 (+19 more)

### Community 82 - "test_prx_loader"
Cohesion: 0.08
Nodes (16): prx_load_result, prx_module, prx_register_fn, ppu_function_count(), main(), prx_export_register(), prx_export_registry_count(), prx_image_load_file() (+8 more)

### Community 83 - "sys_fs"
Cohesion: 0.16
Nodes (27): vm_to_host(), cond_table_lock(), cond_table_unlock(), sys_cond_create(), sys_cond_destroy(), sys_cond_signal(), write_be32(), fill_cell_stat() (+19 more)

### Community 84 - "cellGifDec HLE module"
Cohesion: 0.13
Nodes (26): CellGifDecDataInfo, CellGifDecInfo, CellGifDecInParam, CellGifDecMainHandle, CellGifDecOpnInfo, CellGifDecOutParam, CellGifDecSrc, CellGifDecSubHandle (+18 more)

### Community 85 - "cellSpursJq HLE module"
Cohesion: 0.20
Nodes (26): CellSpursJob256, CellSpursJobId, CellSpursJobQueue, CellSpursJobQueueAttribute, CellSpursJobQueuePort, JqState, s32, u32 (+18 more)

### Community 86 - "nid_harvest"
Cohesion: 0.11
Nodes (23): tools/nid_harvest.py (NID gazetteer), Per-port NID table (weak ppu_hle_register_all), tools/nid_database.py (NID resolver), tools/prx_analyzer.py (dependency graphs), discover_all(), main(), Generate the HLE NID -> handler registration unit from the /* NID: 0x.. */…, Every PRX-style HLE module compiled into the runtime lib. The registration… (+15 more)

### Community 87 - "stb_truetype"
Cohesion: 0.07
Nodes (27): stbrp_init_target(), stbrp_pack_rects(), stbrp_rect, id, w, was_packed, x, y (+19 more)

### Community 88 - "stb_truetype"
Cohesion: 0.25
Nodes (27): stbtt__buf_get(), stbtt__buf_get8(), stbtt__buf_peek8(), stbtt__buf_range(), stbtt__buf_seek(), stbtt__buf_skip(), stbtt__cff_get_index(), stbtt__cff_index_count() (+19 more)

### Community 89 - "stb_truetype"
Cohesion: 0.17
Nodes (27): stbtt_CompareUTF8toUTF16_bigendian(), stbtt_CompareUTF8toUTF16_bigendian_internal(), stbtt__CompareUTF8toUTF16_bigendian_prefix(), stbtt__find_table(), stbtt_FindMatchingFont(), stbtt_FindMatchingFont_internal(), stbtt__get_svg(), stbtt__GetCoverageIndex() (+19 more)

### Community 90 - "sceNp"
Cohesion: 0.15
Nodes (25): s32, SceNpOnlineId, u32, np_build_fake_id(), sceNpGetAccountAge(), sceNpGetAccountRegion(), sceNpGetMyLanguages(), sceNpGetNpId() (+17 more)

### Community 91 - "LV2 syscall dispatch table"
Cohesion: 0.11
Nodes (27): lv2_syscall_fn, lv2_syscall_table, lv2_register_all_syscalls(), lv2_syscall_table, lv2_syscall_dispatch(), lv2_syscall_register(), lv2_syscall_table_init(), lv2_syscall_table (+19 more)

### Community 92 - "sys_event"
Cohesion: 0.16
Nodes (26): bswap64(), event_queue_push(), evt_table_lock(), evt_table_unlock(), flag_check(), sys_event_flag_clear(), sys_event_flag_create(), sys_event_flag_destroy() (+18 more)

### Community 93 - "cellDmux HLE module"
Cohesion: 0.15
Nodes (24): b8, CellDmuxAuInfo, CellDmuxCbEsMsg, CellDmuxCbMsg, CellDmuxEsFilterId, CellDmuxEsHandle, CellDmuxEsResource, CellDmuxHandle (+16 more)

### Community 94 - "cellResc HLE module"
Cohesion: 0.14
Nodes (22): CellRescDsts, CellRescInitConfig, CellRescSrc, s32, u32, u64, cellRescAdjustAspectRatio(), cellRescCreateInterlaceTable() (+14 more)

### Community 95 - "cellSaveData"
Cohesion: 0.30
Nodes (26): CellSaveDataDirList, CellSaveDataFileCallback, CellSaveDataFixedCallback, CellSaveDataListCallback, CellSaveDataSetBuf, CellSaveDataSetList, CellSaveDataStatCallback, build_save_path() (+18 more)

### Community 96 - "cellL10n"
Cohesion: 0.30
Nodes (25): s32, u16, u32, u8, decode_utf16(), decode_utf8(), encode_utf16(), encode_utf8() (+17 more)

### Community 97 - "sceNpMatching2 HLE module"
Cohesion: 0.19
Nodes (24): s32, u32, sceNpMatching2AbortRequest(), sceNpMatching2ContextStart(), sceNpMatching2ContextStop(), sceNpMatching2CreateContext(), sceNpMatching2CreateRoom(), sceNpMatching2DestroyContext() (+16 more)

### Community 98 - "rsx_vp_decompiler"
Cohesion: 0.19
Nodes (23): rsx_vertex_layout_plan, ld_current_vertex_layout(), ld_layout_plan_get(), Out, u32, u8, emit(), emit_src() (+15 more)

### Community 99 - "Guest virtual memory (vm.h)"
Cohesion: 0.13
Nodes (18): check(), main(), vm_host_page(), vm_host_range(), vm_init(), vm_protect(), vm_spu_ls_map(), vm_stack_alloc_init() (+10 more)

### Community 100 - "ELF file loader (ELFFile)"
Cohesion: 0.11
Nodes (10): ELFFile, PRXModuleInfo, PS3 PRX sys_process_prx_info_t structure. Typically located at the start of the…, Represents a parsed PS3 ELF / SELF / PRX file., Read the file and parse all structures., Extract module info from the first PT_LOAD segment of a PRX., Locate import/export tables for a fixed main executable (ET_EXEC). A main EBOOT…, Parse RELA entries from PT_SCE_PPURELA segments and SHT_RELA sections. (+2 more)

### Community 101 - "ppu_disasm"
Cohesion: 0.11
Nodes (23): audit(), machine(), main(), parse_objdump_text(), objdump_audit.py - cross-check our PPU/SPU decoders against the GNU objdump…, (address, word, objdump_mnemonic) for every 4-byte instruction line., run_objdump(), bits() (+15 more)

### Community 102 - "cellSpurs"
Cohesion: 0.18
Nodes (25): CellSpurs, CellSpursWorkloadAttribute, CellSpursWorkloadId, u32, u8, cellSpursAddWorkload(), cellSpursAddWorkloadWithAttribute(), cellSpursAttachLv2EventQueue() (+17 more)

### Community 103 - "sceNpTus HLE module"
Cohesion: 0.22
Nodes (23): s32, s64, u32, sceNpTusAbortRequest(), sceNpTusAddAndGetVariable(), sceNpTusCreateCtx(), sceNpTusDeleteMultiSlotData(), sceNpTusDeleteMultiSlotVariable() (+15 more)

### Community 104 - "dwarf_common"
Cohesion: 0.14
Nodes (19): cstr(), DwarfInfo, open_dwarf(), dwarf_common.py - minimal DWARF2 reader for PS3 debug builds. PS3 SDK (SNC /…, Yield (offset, tag, attrs_dict, depth) for every DIE across all CUs. depth…, Build a DwarfInfo from a loaded elf_parser.ELFFile, or None if no DWARF., Reader over (.debug_info, .debug_abbrev, .debug_str)., Return (value, new_offset). Values: ints for numeric/ref forms (refs are… (+11 more)

### Community 105 - "cellSysutil HLE module"
Cohesion: 0.14
Nodes (22): CellSysutilCallback, cellGame_get_title_id(), s32, u32, cellDiscGameGetBootDiscInfo(), cellDiscGameRegisterDiscChangeCallback(), cellDiscGameUnregisterDiscChangeCallback(), cellSysCacheClear() (+14 more)

### Community 106 - "SPU_LIFTER doc: next priorities"
Cohesion: 0.10
Nodes (24): Next Priorities (RSX draw calls, SPU exec, FFmpeg), RSX Command Processor (NV47xx FIFO, state tracking), cellSpurs (SPU Runtime System, partial), tools/spu_ident.py (name embedded SPU images), Most SPU work is prebuilt middleware, Layer 2: RSX Command Processor (rsx_commands.c), rsx_state struct with dirty flags, libgcm system semaphores / reports area (+16 more)

### Community 107 - "be_t big-endian wrapper type"
Cohesion: 0.15
Nodes (12): be_t, m_data, s32, T, u32, host_addr(), ptr, m_addr (+4 more)

### Community 108 - "sceNpBasic HLE module"
Cohesion: 0.18
Nodes (22): s32, SceNpOnlineId, u32, sceNpBasicAddBlockListEntry(), sceNpBasicGetBlockListEntryCount(), sceNpBasicGetEvent(), sceNpBasicGetFriendListEntry(), sceNpBasicGetFriendListEntryCount() (+14 more)

### Community 109 - "cellGame HLE module"
Cohesion: 0.15
Nodes (18): CellGameContentSize, CellGameSetInitParams, s32, u32, cellGame_init_from_paramsfo(), cellGameBootCheck(), cellGameContentPermit(), cellGameCreateGameData() (+10 more)

### Community 110 - "cellNetCtl HLE module"
Cohesion: 0.14
Nodes (20): cellNetCtlHandler, CellNetCtlInfo, CellNetCtlNatInfo, CellNetCtlNetStartDialogParam, CellNetCtlNetStartDialogResult, s32, u32, cellNetCtlAddHandler() (+12 more)

### Community 111 - "cellOskDialog HLE module"
Cohesion: 0.19
Nodes (20): CellOskDialogCallbackReturnParam, CellOskDialogChar, CellOskDialogInputFieldInfo, CellOskDialogParam, s32, u32, cellOskDialogAbort(), cellOskDialogEnd() (+12 more)

### Community 112 - "cellVoice HLE module"
Cohesion: 0.19
Nodes (21): CellVoiceEventCallback, CellVoiceInitParam, CellVoicePortId, CellVoicePortParam, s32, u32, cellVoiceConnectIPortToOPort(), cellVoiceCreatePort() (+13 more)

### Community 113 - "cellFiber HLE module"
Cohesion: 0.16
Nodes (20): CellFiber, CellFiberAttribute, CellFiberEntry, LPVOID, s32, u32, u64, cellFiberPpuAttributeInitialize() (+12 more)

### Community 114 - "cellGem HLE module"
Cohesion: 0.19
Nodes (20): CellGemAttribute, CellGemInfo, CellGemState, s32, u32, u64, cellGemCalibrate(), cellGemEnableCameraPitchAngleCorrection() (+12 more)

### Community 115 - "sys_ppu_thread syscall"
Cohesion: 0.23
Nodes (21): ppu_thread_info, ppu_context_init(), ppu_set_stack(), find_free_slot(), find_thread(), ppu_host_thread_proc(), ppu_thread_prof_pc(), ppu_thread_register_main() (+13 more)

### Community 116 - "SPU lockstep atomics"
Cohesion: 0.19
Nodes (21): LONG, _InterlockedExchange(), _InterlockedExchangeAdd(), resv_lock(), resv_unlock(), spu_mfc_atomic(), spu_arch_fence(), ls_acquire_locked() (+13 more)

### Community 117 - "cellMic"
Cohesion: 0.17
Nodes (20): CellMicCallback, CellMicDeviceAttr, s32, u32, u64, cellMicClose(), cellMicEnd(), cellMicGetDeviceAttr() (+12 more)

### Community 118 - "cellPngDec HLE module"
Cohesion: 0.23
Nodes (19): CellPngDecInfo, CellPngDecMainHandle, CellPngDecSubHandle, s32, u32, cellPngDecClose(), cellPngDecCreate(), cellPngDecDecodeData() (+11 more)

### Community 119 - "sceNpCommerce2 HLE module"
Cohesion: 0.23
Nodes (19): s32, u32, sceNpCommerce2AbortReq(), sceNpCommerce2CreateCtx(), sceNpCommerce2CreateSessionFinish(), sceNpCommerce2CreateSessionStart(), sceNpCommerce2DestroyCtx(), sceNpCommerce2DestroyReq() (+11 more)

### Community 120 - "lift_audit.py baseline metrics"
Cohesion: 0.15
Nodes (20): lift_audit.py baseline metrics, build_baseline_dict(), build_report(), diff_against_baseline(), _executable_instructions(), fallthrough_census(), _is_terminator_word(), _load_functions_json() (+12 more)

### Community 121 - "gen_vectors"
Cohesion: 0.24
Nodes (21): b2d(), d2b(), emit_fpu(), fp_add(), fp_div(), fp_fcmpu(), fp_fctiwz(), fp_frsp() (+13 more)

### Community 122 - "disasm_audit"
Cohesion: 0.16
Nodes (20): _capstone_or_die(), classify_pair(), decode_ours(), decode_ours_corrupted(), executable_words(), is_whitelisted_by_rule(), main(), make_capstone_decoder() (+12 more)

### Community 123 - "cellMouse HLE module"
Cohesion: 0.21
Nodes (19): CellMouseData, CellMouseDataList, s32, u32, u8, cellMouse_buttonEvent(), cellMouse_moveEvent(), cellMouse_wheelEvent() (+11 more)

### Community 124 - "cellRudp HLE module"
Cohesion: 0.23
Nodes (18): CellRudpCtxId, CellRudpEventHandler, CellRudpOption, s32, u16, u32, cellRudpBind(), cellRudpClose() (+10 more)

### Community 125 - "main"
Cohesion: 0.17
Nodes (19): CONTEXT, DWORD, EXCEPTION_POINTERS, HMODULE, LONG, LPVOID, derive_vfs_root(), dump_threads() (+11 more)

### Community 126 - "test_spu_helpers"
Cohesion: 0.25
Nodes (19): spu_splat_u8(), u128, float_splat(), from_bytes(), main(), make128(), make128_words(), print_u128() (+11 more)

### Community 127 - "audit_guest_ptrs"
Cohesion: 0.14
Nodes (15): Find HLE entry points that call ANOTHER HLE entry point. An entry point…, bare_deref_calls(), function_bodies(), is_guest_entry(), main(), param_is_translated(), pointer_params(), True if the function reassigns `param` through a translation helper. The common… (+7 more)

### Community 128 - "spu_disasm_audit_operands"
Cohesion: 0.16
Nodes (19): audit_one(), canon_channel_token(), canon_reg_token(), canon_spreg_token(), decode_oracle(), decode_ours(), extract_code_segment(), find_oracle_exe() (+11 more)

### Community 129 - "cellUsbd HLE module"
Cohesion: 0.19
Nodes (17): CellUsbdDeviceId, CellUsbdDeviceInfo, CellUsbdLddOps, CellUsbdPipeId, s32, u16, u32, u8 (+9 more)

### Community 130 - "cellVideoOut HLE module"
Cohesion: 0.20
Nodes (17): CellVideoOutConfiguration, CellVideoOutDeviceInfo, CellVideoOutResolution, CellVideoOutState, s32, u16, u32, u8 (+9 more)

### Community 131 - "sceNpSignaling HLE module"
Cohesion: 0.24
Nodes (17): s32, u16, u32, sceNpSignalingActivateConnection(), sceNpSignalingCreateCtx(), sceNpSignalingDeactivateConnection(), sceNpSignalingDestroyCtx(), sceNpSignalingGetConnectionInfo() (+9 more)

### Community 132 - "rsx_commands"
Cohesion: 0.11
Nodes (19): u32, rsx_texture_state, address, border_color, control0, control1, dirty, filter (+11 more)

### Community 133 - "test_adapter_main"
Cohesion: 0.16
Nodes (15): spu_lifted_entry_fn, spu_run_lifted_job(), spu_run_lifted_job_abi(), spu_run_lifted_job_img(), be32(), u128, main(), spu_halt() (+7 more)

### Community 134 - "spu_lifter.py (SPU decode+lift)"
Cohesion: 0.16
Nodes (9): A decoded SPU instruction., SPUInstruction, _chan(), _imm(), LiftedFunction, Channel operand -> a C expression (macro name or numeric id)., The literal target of a direct branch, or None if it has none. The target is…, Condition expression on preferred slot for conditional branches. (+1 more)

### Community 135 - "cellImeJp HLE module"
Cohesion: 0.27
Nodes (16): CellImeJpConfig, CellImeJpHandle, s32, u16, u32, cellImeJpAddChar(), cellImeJpBackspace(), cellImeJpClose() (+8 more)

### Community 136 - "cellSaveData"
Cohesion: 0.19
Nodes (17): CellSaveDataFileSet, CellSaveDataFileStat, CellSaveDataSystemFileParam, dispatch_func_file(), dispatch_func_stat(), ensure_dirs(), enumerate_save_files(), hle_cellSaveDataUserAutoLoad() (+9 more)

### Community 137 - "cellSpurs"
Cohesion: 0.27
Nodes (18): CellSpursEventFlag, EventFlagSync, u16, cellSpursEventFlagClear(), cellSpursEventFlagGetDirection(), cellSpursEventFlagInitialize(), cellSpursEventFlagSet(), cellSpursEventFlagTryWait() (+10 more)

### Community 138 - "cellVdec HLE module"
Cohesion: 0.22
Nodes (16): CellVdecAuInfo, CellVdecCbMsg, CellVdecHandle, CellVdecPicItem, CellVdecResource, CellVdecType, s32, u32 (+8 more)

### Community 139 - "stb_image"
Cohesion: 0.30
Nodes (18): stbi__bit_reverse(), stbi__bitreverse16(), stbi__compute_huffman_codes(), stbi__fill_bits(), stbi__parse_huffman_block(), stbi__parse_uncompressed_block(), stbi__parse_zlib(), stbi__parse_zlib_header() (+10 more)

### Community 140 - "host_posix"
Cohesion: 0.30
Nodes (17): u32, emit(), emit_guest_fp_draw(), emit_guest_fp_tex_draw(), emit_textured_draw(), emit_triangle_draw(), guest_f32(), guest_w32() (+9 more)

### Community 141 - "sys_semaphore"
Cohesion: 0.18
Nodes (15): lbp_breadcrumb_dump(), lbp_hle_complete_pending(), lbp_hang_census(), lbp_unstick_once(), read_be32(), sem_table_lock(), sem_table_unlock(), sys_semaphore_create() (+7 more)

### Community 142 - "SPU interpreter fallback"
Cohesion: 0.15
Nodes (14): spu_lifted_lookup(), spu_decode1(), spu_dispatch(), spu_interp_run(), spu_trace_call(), spu_trace_dump(), sx(), u128 (+6 more)

### Community 143 - "test_taskset_main"
Cohesion: 0.16
Nodes (16): be32(), ev_queue, ev_t, u128, evq_push(), evq_receive(), main(), spu_halt() (+8 more)

### Community 144 - "dwarf_abi"
Cohesion: 0.16
Nodes (16): diff_headers(), extract(), main(), _member_offset(), dwarf_abi.py - authoritative firmware ABI from debug-build DWARF. Unstripped…, Resolve a DW_AT_type reference to a C-ish type string., name -> [(c_type, member_name, is_array)] parsed from the toolkit's headers., Return (mismatches, uncertain). Only structs whose every member is a known… (+8 more)

### Community 145 - "endian_audit"
Cohesion: 0.17
Nodes (17): classify_out_params(), find_c_files(), iter_toplevel_functions(), load_whitelist(), main(), module_priority(), Yield (name, ret_type, params_str, body_start, body_end) for every non-static,…, endian_audit.py -- HLE out-param endianness heuristic auditor (INFO-level).… (+9 more)

### Community 146 - "wrap_spu_elf"
Cohesion: 0.18
Nodes (16): emit_register(), main(), Generate lbp/gen/jobmods_register.c: register every job module's lifted…, Batch-lift the extracted WWS SPU job modules to C. Each module is DMA'd into…, main(), _pack_ehdr(), _pack_phdr(), _pack_shdr() (+8 more)

### Community 147 - "cellHttpUtil HLE module"
Cohesion: 0.30
Nodes (15): CellHttpUtilUri, b64_decode_char(), s32, u32, u8, cellHttpUtilBase64Decode(), cellHttpUtilBase64Encode(), cellHttpUtilBuildUri() (+7 more)

### Community 148 - "gen_vectors"
Cohesion: 0.14
Nodes (17): emit_rot(), frac_to_bits(), mask64(), op_cntlzw(), op_mulhdu(), op_srad(), op_sraw(), op_subfc() (+9 more)

### Community 149 - "xex_parser"
Cohesion: 0.22
Nodes (11): BE, main(), parse(), _parse_execution_info(), _parse_file_format(), _parse_import_libraries(), xex_parser.py -- minimal Xbox 360 XEX2 metadata parser (stdlib only). This is…, Resolve an optional-header entry to its raw bytes (or None if inline). (+3 more)

### Community 150 - "spu_lifter.py branch analysis"
Cohesion: 0.19
Nodes (16): _bi_target_reg(), compute_bi_r0_jumps(), compute_link_returns(), _dest_reg(), _disp_base(), _is_identity_move(), main(), _ops() (+8 more)

### Community 151 - "cellAdec HLE module"
Cohesion: 0.23
Nodes (14): CellAdecAuInfo, CellAdecCbMsg, CellAdecHandle, CellAdecPcmItem, CellAdecResource, CellAdecType, s32, cellAdecClose() (+6 more)

### Community 152 - "cellKb HLE module"
Cohesion: 0.24
Nodes (14): CellKbData, CellKbInfo, s32, u16, u32, cellKb_keyEvent(), cellKbClearBuf(), cellKbEnd() (+6 more)

### Community 153 - "ExportCompareUnits"
Cohesion: 0.23
Nodes (5): ghidra.app.script.GhidraScript, ExportCompareUnits, Override, ExportAnalysisJson, Override

### Community 154 - "sceNpClans"
Cohesion: 0.28
Nodes (15): s32, u32, sceNpClansAbortRequest(), sceNpClansCheckCallback(), sceNpClansCreateClan(), sceNpClansGetClanInfo(), sceNpClansGetMemberList(), sceNpClansInit() (+7 more)

### Community 155 - "test_fp_msl"
Cohesion: 0.23
Nodes (13): u32, u8, check(), main(), put_float(), put_word(), u32, u8 (+5 more)

### Community 156 - "gen_vectors"
Cohesion: 0.14
Nodes (16): emit_cmp(), fp_fcfid(), op_divd(), op_divw(), op_extsb(), op_extsh(), op_extsw(), op_mulhd() (+8 more)

### Community 157 - "pkg_extract"
Cohesion: 0.21
Nodes (14): _aes_encrypt_block(), _aes_expand(), _ecb_encrypt(), _init_aes(), _keystream(), main(), out_path(), pkg_extract.py - extract files from a PS3 .pkg (no RPCS3 GUI needed). Handles… (+6 more)

### Community 158 - "ppu_loader.py (OPD/imports)"
Cohesion: 0.23
Nodes (15): code_ranges(), _cstr(), find_opd(), in_ranges(), main(), parse_imports(), parse_opd(), Parse ET_EXEC firmware imports via the proc_prx_param (PT 0x60000002) ->… (+7 more)

### Community 159 - "spu_ident"
Cohesion: 0.21
Nodes (15): _all_symbols(), classify(), _clean_name(), embedded_spu_images(), fingerprint_table(), main(), spu_ident.py - identify the SPU images embedded in a PS3 executable. Most SPU…, Parse an SPU ELF32 (big-endian) into the code/data-partition landmarks a… (+7 more)

### Community 160 - "cellDaisy"
Cohesion: 0.34
Nodes (14): CellDaisyPipe, CellDaisyPipeAttribute, s32, u32, cellDaisyCreatePipe(), cellDaisyDestroyPipe(), cellDaisyPipeAttributeInitialize(), cellDaisyPipeGetCount() (+6 more)

### Community 161 - "elf_symbols.py + recall bug"
Cohesion: 0.18
Nodes (13): find_functions.py recall bug (bl after early return), load_symbols(), main(), elf_symbols.py - pull the function symbol table out of an unstripped PS3 ELF.…, Every STT_FUNC symbol with a real code address, as {start,end,size,name}., Function start addresses from either tool's JSON (bare list or {functions:[]})., score(), _starts() (+5 more)

### Community 162 - "NID SHA-1 computation (nid.h)"
Cohesion: 0.25
Nodes (11): ps3_compute_nid(), ps3_compute_nid_ex(), ps3_sha1_final(), ps3_sha1_init(), ps3_sha1_rotl(), ps3_sha1_transform(), ps3_sha1_update(), ps3_sha1_ctx (+3 more)

### Community 163 - "PPU guest-call runtime (ppu_run)"
Cohesion: 0.16
Nodes (15): harness_guest_caller(), GetCurrentThreadId(), ppu_guest_call(), ppu_guest_call_ct(), ppu_lookup(), ppu_opd_resolve(), ppu_resv_register(), ppu_run() (+7 more)

### Community 164 - "sceNpUtil HLE module"
Cohesion: 0.22
Nodes (13): s32, u32, sceNpUtilBandwidthTestAbort(), sceNpUtilBandwidthTestGetStatus(), sceNpUtilBandwidthTestInitStart(), sceNpUtilBandwidthTestShutdown(), sceNpUtilCheckOnlineId(), sceNpUtilCmpNpId() (+5 more)

### Community 165 - "test_event_main"
Cohesion: 0.18
Nodes (13): be32(), ev_queue, ev_t, u128, evq_push(), evq_receive(), main(), spu_halt() (+5 more)

### Community 166 - "test_lfqueue_main"
Cohesion: 0.18
Nodes (13): be32(), ev_queue, ev_t, u128, evq_push(), evq_receive(), main(), spu_halt() (+5 more)

### Community 167 - "lift_prx"
Cohesion: 0.23
Nodes (10): be16(), be32(), be64(), cstr(), main(), Prx, Exports/imports from the RELOCATED image (tables hold real EAs)., (vaddr, size) of the first (text) LOAD segment. (+2 more)

### Community 168 - "spu_disasm.py (SPU decoder)"
Cohesion: 0.16
Nodes (14): bits(), disassemble_spu(), main(), # NOTE: br/brsl/bra/brasl are RI16 (9-bit op 0x060/0x062/0x064/0x066), NOT, # NOTE: RI16 must be checked BEFORE RI10 in the decoder because some RI16, # NOTE: the rotate/shift IMMEDIATE forms (roti/rotmi/rotmai/shli/rothi/, SPU (Synergistic Processing Unit) disassembler for PS3 binaries. Decodes 32-bit…, Sign-extend *value* from *nbits* to a Python int. (+6 more)

### Community 169 - "cellAvconfExt"
Cohesion: 0.32
Nodes (13): CellAudioOutConfiguration, CellAudioOutDeviceInfo, s32, u32, cellAudioOutConfigure(), cellAudioOutGetConfiguration(), cellAudioOutGetDeviceInfo(), cellAudioOutGetNumberOfDevice() (+5 more)

### Community 170 - "cellMsgDialog HLE module"
Cohesion: 0.29
Nodes (12): CellMsgDialogCallback, CellMsgDialogType, s32, u32, cellMsgDialogAbort(), cellMsgDialogClose(), cellMsgDialogOpen2(), cellMsgDialogProgressBarInc() (+4 more)

### Community 171 - "cellSsl"
Cohesion: 0.34
Nodes (13): CellSslCertId, s32, u32, u64, u8, cellSslCertGetNotAfter(), cellSslCertGetNotBefore(), cellSslCertGetPublicKey() (+5 more)

### Community 172 - "SYSCALLS doc: platform switches"
Cohesion: 0.15
Nodes (14): cellFs (file I/O with path mapping), cellPad (XInput / SDL2 GameController), Inline platform switches (#ifdef _WIN32), Networking (Winsock2 / POSIX sockets), Sync Primitives (CRITICAL_SECTION / pthread_mutex), Threading (CreateThread / pthread_create), tools/dwarf_abi.py (--diff-headers), PS3 Error Codes (0x8XXYYZZ) (+6 more)

### Community 173 - "LBP CMake build graph"
Cohesion: 0.20
Nodes (14): cellSpurs workload fingerprint dispatch, clang-cl toolchain requirement, lbp_ps3 recompiled executable, Lifted PPU recomp chunk TUs, Lifted SPU images and workload registration, Linker map for crash RVA resolution, LittleBigPlanet recompiled via ps3recomp, Per-game PPU glue scaffold (ppu_loader/hle/sysprx/fs) (+6 more)

### Community 174 - "sceNpCommerce HLE module"
Cohesion: 0.27
Nodes (12): s32, u32, sceNpCommerce2DoCheckoutStartAsync(), sceNpCommerce2DoEntitlementListFinishAsync(), sceNpCommerce2DoEntitlementListStartAsync(), sceNpCommerce2GetCategoryInfo(), sceNpCommerce2GetProductInfo(), sceNpCommerce2GetResult() (+4 more)

### Community 175 - "yz_frontier_trace"
Cohesion: 0.27
Nodes (13): rsx_live_draw_note_inline_transfer(), yz_frontier_count(), yz_frontier_event_name(), yz_frontier_next(), yz_frontier_now(), yz_frontier_publish(), yz_frontier_trace_arm(), yz_frontier_trace_dump() (+5 more)

### Community 176 - "elf_parser"
Cohesion: 0.15
Nodes (6): ProgramHeader, Single ELF program header (PHDR) entry., ELF RELA relocation entry., Convert a virtual address to a file offset using PT_LOAD segments., RelaEntry, vaddr_to_offset()

### Community 177 - "cellBGDL"
Cohesion: 0.31
Nodes (12): CellBgdlId, CellBgdlInfo, s32, u32, cellBgdlCancelDownload(), cellBgdlGetInfo(), cellBgdlGetList(), cellBgdlInit() (+4 more)

### Community 178 - "cellHttps"
Cohesion: 0.32
Nodes (12): CellHttpsCertInfo, CellHttpsData, CellHttpsHandle, s32, u32, cellHttpsClearCerts(), cellHttpsEnd(), cellHttpsGetCertInfo() (+4 more)

### Community 179 - "cellMusicDecode2"
Cohesion: 0.24
Nodes (12): CellMusicDecode2Callback, CellMusicDecode2Info, s32, u32, cellMusicDecode2Finalize(), cellMusicDecode2GetDecodeStatus(), cellMusicDecode2GetInfo(), cellMusicDecode2GetSelectionContext() (+4 more)

### Community 180 - "cellSheap HLE module"
Cohesion: 0.35
Nodes (11): CellSheapAttr, CellSheapHandle, align_up(), s32, u32, cellSheapAllocate(), cellSheapFinalize(), cellSheapFree() (+3 more)

### Community 181 - "cellSubdisplay HLE module"
Cohesion: 0.23
Nodes (11): CellSubdisplayConfig, CellSubdisplayTouchData, s32, u32, cellSubdisplayEnd(), cellSubdisplayGetRequiredMemory(), cellSubdisplayGetTouchData(), cellSubdisplayInit() (+3 more)

### Community 182 - "cellUserInfoSelectUser HLE module"
Cohesion: 0.27
Nodes (12): CellUserInfoFinishCallback, CellUserInfoListSet, CellUserInfoUserList, CellUserInfoUserStat, s32, u32, cellUserInfoEnableOverlay(), cellUserInfoGetHomeDir() (+4 more)

### Community 183 - "spurs_taskset"
Cohesion: 0.23
Nodes (9): spurs_pm_build_context(), spurs_pm_mark_running(), spurs_pm_select_task(), spurs_bitset_clear(), spurs_bitset_set(), spurs_bitset_test(), spurs_taskset_add_task(), spurs_taskset_init() (+1 more)

### Community 184 - "spu_disasm_audit"
Cohesion: 0.23
Nodes (12): audit_one(), decode_oracle(), decode_ours(), extract_code_segment(), find_oracle_exe(), main(), Counter, Return (code_bytes, vaddr) for the executable PT_LOAD segment. (+4 more)

### Community 185 - "cellAdecCelp8 HLE module"
Cohesion: 0.27
Nodes (10): CellAdecCelp8Config, CellAdecCelp8Handle, s32, u32, cellAdecCelp8Close(), cellAdecCelp8Decode(), cellAdecCelp8Open(), cellAdecCelp8Reset() (+2 more)

### Community 186 - "cellVdecDivx HLE module"
Cohesion: 0.27
Nodes (10): CellVdecDivxConfig, CellVdecDivxFrameInfo, CellVdecDivxHandle, s32, u32, cellVdecDivxClose(), cellVdecDivxDecode(), cellVdecDivxOpen() (+2 more)

### Community 187 - "test_torture_main"
Cohesion: 0.18
Nodes (10): fnp, u128, main(), spu_halt(), spu_indirect_branch(), spu_rchcnt(), spu_rdch(), spu_register_function() (+2 more)

### Community 188 - "sceNpSns HLE module"
Cohesion: 0.26
Nodes (10): s32, u32, sceNpSnsCheckCallback(), sceNpSnsFbCheckThisUserAccount(), sceNpSnsFbGetLongAccessToken(), sceNpSnsFbPostFeedMessage(), sceNpSnsInit(), sceNpSnsTerm() (+2 more)

### Community 189 - "sys_interrupt"
Cohesion: 0.27
Nodes (10): s32, u32, u64, sys_interrupt_tag_create(), sys_interrupt_tag_destroy(), sys_interrupt_thread_disestablish(), sys_interrupt_thread_eoi(), sys_interrupt_thread_establish() (+2 more)

### Community 190 - "compare harness README"
Cohesion: 0.18
Nodes (12): Cross-platform comparison harness, ingest_native.py PS3 units builder, match_functions.py string/const/callgraph matcher, Shared 64-bit big-endian PowerPC codebase anchoring, Unified Unit/Module schema (schema.py), VMX128 / 360-only idiom mining, XenonRecomp / rexglue Xbox 360 recompiler, Compatibility matrix and failure-mode catalog (+4 more)

### Community 191 - "cellMusicDecode"
Cohesion: 0.29
Nodes (10): CellMusicDecodeCallback, s32, u32, cellMusicDecodeFinish(), cellMusicDecodeGetDecodeStatus(), cellMusicDecodeGetSelectionContext(), cellMusicDecodeInit(), cellMusicDecodeRead() (+2 more)

### Community 192 - "cellOvis HLE module"
Cohesion: 0.33
Nodes (9): CellOvisHandle, s32, u32, cellOvisCreateOverlay(), cellOvisDestroyOverlay(), cellOvisGetOverlayTableSize(), cellOvisInit(), cellOvisInvalidateOverlay() (+1 more)

### Community 193 - "cellFsUtility HLE module"
Cohesion: 0.31
Nodes (9): s32, u32, u64, cellFsUtilCopyFile(), cellFsUtilExists(), cellFsUtilGetFileSize(), cellFsUtilMkdirAll(), cellFsUtilReadFile() (+1 more)

### Community 194 - "build_spu_workloads"
Cohesion: 0.35
Nodes (10): be16(), be32(), debase_includes(), fnv1a64(), img_size(), is_spu_elf(), main(), build_spu_workloads.py -- lift a title's SPU images and emit a workload… (+2 more)

### Community 195 - "check_ppu_scaffold.py ratchet"
Cohesion: 0.25
Nodes (10): compiler_family(), count_errors(), main(), pick_toolchain(), Return (key, argv-prefix). The key names the row in the baseline., Diagnostic lines only. Matching a bare "error" also caught clang's "1 error…, Compile-check the PPU boot scaffold, on whatever platform you are standing on.…, The lifter's real header preamble, plus the few externs the generated header… (+2 more)

### Community 196 - "parse_rrc"
Cohesion: 0.22
Nodes (3): parse(), R, Parse an RPCS3 RSX capture (.rrc.gz) and dump a draw-list manifest. Ground-…

### Community 197 - "cellFreeType HLE module"
Cohesion: 0.31
Nodes (8): CellFreeTypeConfig, CellFreeTypeLibrary, CellFreeTypeVersion, s32, cellFreeTypeEnd(), cellFreeTypeGetLibrary(), cellFreeTypeGetVersion(), cellFreeTypeInit()

### Community 198 - "cellKey2char"
Cohesion: 0.40
Nodes (9): CellKey2CharHandle, s32, u16, u32, cellKey2CharClose(), cellKey2CharGetMode(), cellKey2CharKeyCodeToChar(), cellKey2CharOpen() (+1 more)

### Community 199 - "cellVideoUpload HLE module"
Cohesion: 0.29
Nodes (8): CellVideoUploadCallback, CellVideoUploadParam, s32, cellVideoUploadCancel(), cellVideoUploadGetStatus(), cellVideoUploadInit(), cellVideoUploadStart(), cellVideoUploadTerm()

### Community 200 - "cellSysmodule"
Cohesion: 0.51
Nodes (9): s32, u16, cellSysmoduleFinalize(), cellSysmoduleInitialize(), cellSysmoduleIsLoaded(), cellSysmoduleLoadModule(), cellSysmoduleUnloadModule(), sysmodule_id_to_name() (+1 more)

### Community 201 - "spu_drain"
Cohesion: 0.22
Nodes (8): spu_halt(), spu_rchcnt(), spu_depth_guard(), spu_img_restore(), spu_irq_regs_maybe_restore(), spu_irq_regs_save(), spu_sn_defer_ticks(), spu_task_launch_check()

### Community 202 - "test_brsl_return_main"
Cohesion: 0.22
Nodes (8): u128, main(), spu_halt(), spu_indirect_branch(), spu_rchcnt(), spu_rdch(), spu_stop(), spu_wrch()

### Community 203 - "test_shufb_main"
Cohesion: 0.22
Nodes (8): u128, main(), spu_halt(), spu_indirect_branch(), spu_rchcnt(), spu_rdch(), spu_stop(), spu_wrch()

### Community 204 - "test_sum_main"
Cohesion: 0.22
Nodes (8): u128, main(), spu_halt(), spu_indirect_branch(), spu_rchcnt(), spu_rdch(), spu_stop(), spu_wrch()

### Community 205 - "test_workload_main"
Cohesion: 0.22
Nodes (7): u128, spu_halt(), spu_indirect_branch(), spu_rchcnt(), spu_rdch(), spu_stop(), spu_wrch()

### Community 206 - "run_torture"
Cohesion: 0.42
Nodes (9): build_guest(), build_port(), group_fails(), lift(), main(), parse(), ps3recomp lifter torture-suite runner. For each optimization variant (-O0, -O2…, run() (+1 more)

### Community 207 - "ghidra_names.py address map"
Cohesion: 0.33
Nodes (9): is_meaningful_name(), load(), main(), norm_addr(), ghidra_names.py — build a {addr: name} map from Ghidra analysis JSON. Consumes…, Turn an arbitrary string into a C identifier fragment., Derive (confidence, name_fragment) from a referenced string. confidence: 3 =…, sanitize_ident() (+1 more)

### Community 208 - "cellGameExec HLE module"
Cohesion: 0.31
Nodes (6): CellGameExecBootParam, s32, u32, cellGameGetBootGameInfo(), cellGameGetExitParam(), cellGameSetExitParam()

### Community 209 - "host_main.c guest invoke"
Cohesion: 0.33
Nodes (6): build_command_buffer(), build_guest_memory(), cmd_be32(), cmd_method(), put_bef(), put_fp_word()

### Community 210 - "cellSpurs"
Cohesion: 0.33
Nodes (9): DWORD, LPVOID, jc_execute(), jc_signal_done(), jc_thread(), jg_wait(), pm_write_watch(), spurs_kernel_thread() (+1 more)

### Community 211 - "guest-pointer fault trap"
Cohesion: 0.19
Nodes (9): flow_alloc_rec, id, lr, ptr, size, ps3_fault_report(), ps3_install_guest_ptr_trap(), sigaction (+1 more)

### Community 212 - "test_dma_main"
Cohesion: 0.25
Nodes (7): u128, spu_halt(), spu_indirect_branch(), spu_rchcnt(), spu_rdch(), spu_stop(), spu_wrch()

### Community 213 - "gen_vectors"
Cohesion: 0.25
Nodes (9): cr_signed(), emit_dot(), emit_r1(), emit_ri(), emit_rr(), main(), Record forms: value + CR0 (lifter must derive cr0 from the 64-bit signed…, Immediate forms (baked into the asm string). (+1 more)

### Community 214 - "extract_jobmods"
Cohesion: 0.33
Nodes (8): find_modules(), _inflate_at(), is_module(), main(), Extract WWS SPU job code modules (C0DEC0DE) from LBP's SDATA archives.…, Inflate the zlib stream at offset i. Return (decompressed, compressed_len) or…, Yield each SPU module. The FSHb stores files as 32 KiB-chunked zlib streams: a…, sdata_decrypt()

### Community 215 - "find_indirect_entries.py (session 6)"
Cohesion: 0.31
Nodes (8): find_entries(), _is_terminator(), main(), Find alternate function entry points that only a runtime pointer reaches. The…, Program headers of a PPC64 big-endian ELF: (vaddr, offset, filesz, flags)., Unconditional branch (b/ba), blr or bctr -- nothing falls through these., Start addresses of alternate-entry table slots. A slot ends in `li r11, N ; b…, segments()

### Community 216 - "generate_stubs.py (HLE stub gen)"
Cohesion: 0.33
Nodes (8): generate_master_header(), generate_module_stubs(), main(), Generate a master header that includes all module stubs., Auto-generate HLE stub C files for PS3 modules. Reads the NID database (built-…, Make a string safe for use as a C identifier., Generate .h and .c files for a module. Args: module: Module name (e.g.…, _safe_identifier()

### Community 217 - "gen_test_workload"
Cohesion: 0.39
Nodes (6): ch(), SPU workload-dispatch test program. Exercises runtime/spu/spu_workload.c — the…, ri10(), ri16(), rr(), w()

### Community 218 - "disasm operand-divergence whitelist"
Cohesion: 0.32
Nodes (8): PPU disasm operand-format benign-divergence whitelist, FPR shown as rN instead of fN (cosmetic prefix bug), tools/ppu_disasm.py PPU decoder, vmaddfp/vnmsubfp operand-order convention divergence, SPU disasm operand-audit whitelist, RI7 unsigned-vs-signed sign-extension decode bug, RPCS3 SPUDisAsm.h oracle, tools/spu_disasm.py SPU decoder

### Community 219 - "cellPhotoExport"
Cohesion: 0.38
Nodes (6): CellPhotoExportCallback, CellPhotoExportParam, s32, cellPhotoExportEnd(), cellPhotoExportInit(), cellPhotoExportStart()

### Community 220 - "cellScreenshot"
Cohesion: 0.43
Nodes (6): CellScreenShotSetParam, s32, cellScreenShotDisable(), cellScreenShotEnable(), cellScreenShotSetOverlayImage(), cellScreenShotSetParameter()

### Community 221 - "cellSpurs"
Cohesion: 0.48
Nodes (7): CellSpursTaskAttribute, CellSpursTaskId, CellSpursTaskset, cellSpursCreateTask(), cellSpursCreateTaskWithAttribute(), cellSpursJoinTask(), cellSpursSendSignal()

### Community 222 - "cellRemotePlay"
Cohesion: 0.43
Nodes (6): s32, u32, cellRemotePlayEnd(), cellRemotePlayGetStatus(), cellRemotePlayInit(), cellRemotePlayIsAvailable()

### Community 223 - "requirements"
Cohesion: 0.38
Nodes (7): capstone (PowerPC disassembly), construct (structured binary parsing), pycryptodome (ELF/SELF decryption), ps3recomp Python tool dependencies, tabulate (CLI output), tqdm (progress bars), ps3recomp tools dependencies

### Community 224 - "gen_test_adapter"
Cohesion: 0.48
Nodes (6): ch(), SPU bring-up rung 5 brick — lifted-job adapter / SPURS task ABI. The SPU job…, ri10(), ri16(), rr(), w()

### Community 225 - "gen_test_bringup"
Cohesion: 0.48
Nodes (6): ch(), SPU bring-up prototype. Demonstrates the PPU<->SPU coordination handshake the…, ri10(), ri16(), rr(), w()

### Community 226 - "gen_test_dma"
Cohesion: 0.48
Nodes (6): ch(), Test #4: MFC DMA GET round-trip (vm_base+0 -> LS+0x100 -> lqd -> mbox)., ri10(), ri16(), rr(), w()

### Community 227 - "gen_test_event"
Cohesion: 0.48
Nodes (6): ch(), SPU bring-up rung 2 — event-driven completion. Like the bring-up test, the SPU…, ri10(), ri16(), rr(), w()

### Community 228 - "gen_test_shufb"
Cohesion: 0.48
Nodes (6): ch(), Test #2: shufb with special selector patterns (0xC0/0xE0/0x80 magic bytes)., ri16(), rr(), rrr(), w()

### Community 229 - "gen_test_sum"
Cohesion: 0.48
Nodes (6): ch(), Test #1: sum loop -- reads N values from inbound mailbox, sums them, writes the…, ri10(), ri16(), rr(), w()

### Community 230 - "ida_export_units"
Cohesion: 0.43
Nodes (6): _args(), _func_unit(), main(), ida_export_units.py -- IDAPython script, emits units.json matching schema.py.…, Return decoded string at ea if a string is defined there, else None., _string_at()

### Community 231 - "extract_spu_images"
Cohesion: 0.38
Nodes (6): find_spu_images(), main(), parse_spu_elf_size(), Scan a PPU ELF / EBOOT for embedded SPU ELF images and extract each one. PS3…, Return the byte-size of the SPU ELF starting at `off`, or None if it doesn't…, Yield (offset, size) for each embedded SPU ELF.

### Community 232 - "spu_lift_selftest"
Cohesion: 0.43
Nodes (6): gen_driver(), main(), spu_lift_selftest.py - end-to-end SPU lifter validation against native…, name -> local-store address for wanted STT_FUNC symbols in an SPU ELF32 (BE).…, sh(), spu_func_addrs()

### Community 233 - "cellPhotoImport"
Cohesion: 0.47
Nodes (5): CellPhotoImportCallback, s32, cellPhotoImportEnd(), cellPhotoImportInit(), cellPhotoImportStart()

### Community 234 - "cellPrint"
Cohesion: 0.47
Nodes (5): s32, u32, cellPrintEnd(), cellPrintGetPrinterCount(), cellPrintInit()

### Community 235 - "icon_1024"
Cohesion: 0.40
Nodes (6): macOS App Icon (1024px), Recompiled Game Host App Bundle, Navy-to-Purple Vertical Gradient Background, Red Triangle / Mountain Peak Mark, Rounded-Square Icon Silhouette, Uncharted Exploration / Summit Theme

### Community 236 - "gen_test_brsl_return"
Cohesion: 0.53
Nodes (5): ch(), Test #3: brsl + bi $r0 (SPU ABI call/return)., ri16(), rr(), w()

### Community 237 - "gen_test_lfqueue"
Cohesion: 0.53
Nodes (5): ch(), SPU bring-up rung 3 — cellSync LFQueue work channel. The PPU enqueues a work…, ri16(), rr(), w()

### Community 238 - "gen_test_taskset"
Cohesion: 0.53
Nodes (5): ch(), SPU bring-up rung 4 — cellSpurs taskset / job-queue throughput. The SPU job is…, ri16(), rr(), w()

### Community 239 - "rebase_spu_elf"
Cohesion: 0.53
Nodes (5): be16(), be32(), main(), Rebase a PIC SPU overlay ELF (SCE .fixup scheme) to its runtime load base.…, wbe32()

### Community 240 - "spbalance"
Cohesion: 0.47
Nodes (5): check(), main(), parse(), spbalance.py - find stack-imbalance bugs in lifted PPU output. Lifted functions…, name -> dict(alloc, real_restores=[ints], tramps=set(names), has_real_ret)

### Community 241 - "RSX_FRAGMENT_PROGRAM"
Cohesion: 0.40
Nodes (5): rsx_fp_decompiler (NV40 to HLSL), Mesa/nouveau nvfx_shader.h (bit layout source), RSX Fragment Program NV40 (nvfx) ISA, FP word order swap16halves, RSX Shader Translation Strategy (NV40 ISA)

### Community 242 - "cellLicenseArea"
Cohesion: 0.60
Nodes (4): s32, cellLicenseAreaCheck(), cellLicenseAreaGetAreaCode(), cellLicenseAreaIsValid()

### Community 246 - "callgraph"
Cohesion: 0.60
Nodes (4): build(), load_or_build(), main(), Build a call graph over lifted PPU output, and answer reachability questions.…

### Community 247 - "gen_spu_workloads"
Cohesion: 0.60
Nodes (3): be16(), be32(), img_size()

### Community 248 - "inject_spcheck"
Cohesion: 0.60
Nodes (4): inject(), main(), inject_spcheck.py - instrument lifted chunks for the dynamic stack-imbalance…, revert()

### Community 249 - "ppu_loader"
Cohesion: 0.50
Nodes (4): PpuOpdFixup, code, opd, toc

### Community 252 - "main"
Cohesion: 0.67
Nodes (4): EXCEPTION_POINTERS, LONG, torture_crash_filter(), vm_commit_veh()

### Community 253 - "gen_spu_interp"
Cohesion: 0.67
Nodes (3): emit_lookup(), enum_name(), Generate the SPU interpreter's decode tables (C) from the validated Python…

### Community 254 - "ghidra_analyze"
Cohesion: 0.67
Nodes (3): find_ghidra(), main(), ghidra_analyze.py — automate Ghidra headless analysis of a PS3 PPU ELF. Imports…

### Community 255 - "test_boot_posix"
Cohesion: 0.67
Nodes (3): main(), End-to-end boot test: synthetic ELF -> loader -> lifter -> compile -> run. The…, run()

### Community 257 - "MODULES_REFERENCE"
Cohesion: 0.67
Nodes (3): cellFiber (Windows Fibers / ucontext), Fiber / ucontext (deprecated on macOS), NSWindow main-thread fix (dispatch on main queue)

### Community 263 - "CMakeLists"
Cohesion: 0.67
Nodes (3): lv2 sync sources (sys_mutex/cond/semaphore/event), Seeded-bug self-test via mirrored runtime tree, sync_stress lv2 sync-primitive stress suite

## Knowledge Gaps
- **283 isolated node(s):** `hi`, `lo`, `name`, `loaded`, `func_table` (+278 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1288 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **20 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `SPU disasm operand-audit whitelist` connect `disasm operand-divergence whitelist` to `Lifted SPU: mixed images`, `spu_lifter.py branch analysis`?**
  _High betweenness centrality (0.239) - this node is a cross-community bridge._
- **Why does `PPU disasm operand-format benign-divergence whitelist` connect `disasm operand-divergence whitelist` to `post_lift.py + ppu_lifter.py`?**
  _High betweenness centrality (0.195) - this node is a cross-community bridge._
- **Why does `spu_context` connect `Lifted SPU: mixed images 2` to `Lifted SPU: libsre kernel`, `Lifted SPU: libsre kernel2`, `Lifted SPU: mixed images`, `spu_recomp`, `Lifted SPU: CRI job`, `spu_recomp`, `spu_recomp`, `spu_recomp`, `test_adapter_main`, `SPU interpreter fallback`, `test_taskset_main`, `Lifted SPU: lifted`, `test_event_main`, `test_lfqueue_main`, `SPU MFC DMA channels`, `SPU context + channels`, `spu_recomp_register entry points`, `test_torture_main`, `win32_compat threading shim`, `spu_workload`, `spu_drain`, `Lifted SPU: cri_job_lifted`, `test_brsl_return_main`, `test_shufb_main`, `test_sum_main`, `test_workload_main`, `test_dma_main`, `SPU lockstep atomics`, `spu_recomp`, `spu_recomp`, `spu_recomp`, `main`?**
  _High betweenness centrality (0.099) - this node is a cross-community bridge._
- **Are the 444 inferred relationships involving `spu_ls_read128()` (e.g. with `spu_func_00000030()` and `spu_func_00000088()`) actually correct?**
  _`spu_ls_read128()` has 444 INFERRED edges - model-reasoned connections that need verification._
- **Are the 407 inferred relationships involving `spu_ls_write128()` (e.g. with `spu_func_00000030()` and `spu_func_000000D0()`) actually correct?**
  _`spu_ls_write128()` has 407 INFERRED edges - model-reasoned connections that need verification._
- **Are the 400 inferred relationships involving `spu_ai()` (e.g. with `spu_func_00000030()` and `spu_func_00000088()`) actually correct?**
  _`spu_ai()` has 400 INFERRED edges - model-reasoned connections that need verification._
- **Are the 383 inferred relationships involving `spu_il()` (e.g. with `spu_func_000000D0()` and `spu_func_000001B8()`) actually correct?**
  _`spu_il()` has 383 INFERRED edges - model-reasoned connections that need verification._