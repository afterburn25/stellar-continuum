#version 450
// Depth-only pass: no colour targets, no outputs. Present so the pipeline
// carries a valid fragment stage on backends that require one.
void main() {}
