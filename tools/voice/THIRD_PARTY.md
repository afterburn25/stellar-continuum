# Optional English neural voice pack

The pack is installed explicitly. Inference is local and does not send game text to a service. No real person's voice is cloned by this project. Candidate voice identities are model-provided embeddings; casting remains subject to listening review.

## Model and voice data

Kokoro-82M v1.0 by hexgrad is published under Apache-2.0, as declared in its [model card](https://huggingface.co/hexgrad/Kokoro-82M). The FP32 ONNX export and voice NPZ come from [kokoro-onnx model-files-v1.0](https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.0). Exact downloaded SHA-256 values are pinned in setup_kokoro.py. The upstream model card documents training provenance and credits; preserve it and the Apache license in a released pack. These declarations permit evaluating commercial distribution; they do not constitute a project legal clearance or guarantee about every training source.

## Runtime

ONNX Runtime is MIT, NumPy BSD-3-Clause, Misaki 0.9.4 Apache-2.0, spaCy and en_core_web_sm 3.8.0 MIT. Misaki's optional eSpeak/phonemizer packages, Torch and transformer models are not installed by this pack. num2words 0.5.14 is LGPL-2.1-or-later: its installed license and complete replaceable Python library source are copied into notices. Dependencies remain separately installed and unmodified. collect_notices.py records exact versions, original metadata and installed license/notice files. Further nested dependencies retain their original notices; inspect that inventory before redistributing a bundled pack.

The current installer creates a local Python 3.12 virtual environment. It relies on the user's existing Python installation and is not a portable redistributable. A self-contained runtime bundle, its corresponding notices/sources, and clean-machine packaging checks remain a release step.

## ONNX input convention and vocabulary attribution

The worker's token/style/speed tensor convention and vocabulary follow kokoro-onnx by thewh1teagle. The vocabulary is stored separately in kokoro-vocab.json. The applicable upstream notice follows:

MIT License

Copyright (c) 2025 github.com/thewh1teagle

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
