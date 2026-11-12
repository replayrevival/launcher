#pragma once

// RCSS decorators for the blueprint look. Call registerDecorators() after Rml::Initialise().
//   decorator: chamfer(<cut dp> <outline dp> <fill colour> <outline colour>);
//   decorator: arrow(<colour>);      (right-pointing selection marker)
// All sizes are in dp, so they follow the context's density-independent pixel ratio.
void registerDecorators();
