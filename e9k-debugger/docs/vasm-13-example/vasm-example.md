# Basic source level debugging with VASM on Kickstart 1.3

## Configure debugger

Firstly configure the debugger:

 - **UAE Config** - create a new UAE config - this does not need futher adjustment
 - **DH0 Folder** - select the docs/vasm-13-example/DH0/ folder - this will be the Amiga HD we use:

     `/Users/user/e9k-debugger/docs/vasm-13-example/DH0/`
 - **EXE** - must point to the Amiga hunk executable with debug info - for this example this is:

     `/Users/user/e9k-debugger/docs/vasm-13-example/example_syms.exe`
 - **Toolchain Prefex** - select `vasm`
 - **Source Folder** - in our example this is:

    `/Users/user/e9k-debugger/docs/vasm-13-example/`

 ## Review Startup-Sequence
 
 We launch our example exe on the Amiga HD via the `Startup-sequence` in `docs/vasm-13-example/DH0/S`
 
 `load9013 --break example_run.exe`
 
 If we don't want to break on startup, we can remove `--break`
 
 ## Start debugger
 
 The debugger should now start and break at the entry point to our example program
 
Selecting `SRC` in the source pane should now show vasm level source code.
