#pragma once

namespace solero {

// How a mod's extracted files should be normalized into its staging dir.
struct InstallLayout {
    int  stripComponents = 0; // leading path components to drop (wrapper dir)
    bool wrapInData = false;  // prepend "Data/" to each file after stripping
    bool isFomod = false;     // a fomod/ModuleConfig.xml was found
    int  fomodRootLevel = 0;  // components before the fomod dir (for Stage 3B)
};

} // namespace solero
