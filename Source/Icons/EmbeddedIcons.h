#pragma once

namespace EmbeddedIcons
{
inline constexpr const char* saveSvg = R"svg(
<svg fill="#000000" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
  <g id="save-filled">
    <path d="M19,0H1C0.448,0,0,0.448,0,1v22c0,0.552,0.448,1,1,1h22c0.552,0,1-0.448,1-1V5L19,0z M6,3c0-0.552,0.448-1,1-1h10 c0.552,0,1,0.448,1,1v6c0,0.552-0.448,1-1,1H7c-0.552,0-1-0.448-1-1V3z M20,22H4v-7c0-0.552,0.448-1,1-1h14c0.552,0,1,0.448,1,1V22 z"/>
    <path d="M16,9h-4V3h4V9z"/>
  </g>
</svg>
)svg";

inline constexpr const char* deleteSvg = R"svg(
<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <path fill="#000000" d="M352 192V95.936a32 32 0 0 1 32-32h256a32 32 0 0 1 32 32V192h256a32 32 0 1 1 0 64H96a32 32 0 0 1 0-64h256zm64 0h192v-64H416v64zM192 960a32 32 0 0 1-32-32V256h704v672a32 32 0 0 1-32 32H192zm224-192a32 32 0 0 0 32-32V416a32 32 0 0 0-64 0v320a32 32 0 0 0 32 32zm192 0a32 32 0 0 0 32-32V416a32 32 0 0 0-64 0v320a32 32 0 0 0 32 32z"/>
</svg>
)svg";

// Dice icon for the Randomize button
inline constexpr const char* randomSvg = R"svg(
<svg viewBox="0 0 20 20" xmlns="http://www.w3.org/2000/svg">
  <rect x="2" y="2" width="16" height="16" rx="3" ry="3" fill="none" stroke="#000000" stroke-width="1.4"/>
  <circle cx="6"  cy="6"  r="1.3" fill="#000000"/>
  <circle cx="14" cy="6"  r="1.3" fill="#000000"/>
  <circle cx="10" cy="10" r="1.3" fill="#000000"/>
  <circle cx="6"  cy="14" r="1.3" fill="#000000"/>
  <circle cx="14" cy="14" r="1.3" fill="#000000"/>
</svg>
)svg";
}

