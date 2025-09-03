This was a first attempt at an animation that was intended to be used as a background for my [personal website](https://lithx.dev/). Raylib is a very intuitive library that allowed me to prototype and work out all of the logic relatively quickly.
Initially, I was going to compile this straight to the web using [emscripten](https://emscripten.org/) to compile everything to WASM, but it became clear quickly that hooking this sort of C compilation into an Astro/Vite project would become a headache all on its own.

I eventually settled on manually porting the working script (`main2.c`) over to Typescript and WebGL, refining further from there. You can find the port in [this repository](https://github.com/lith-x/lith-x.github.io) under `src/ts/bg-webgl.ts` (as of writing).
