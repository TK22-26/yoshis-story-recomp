# Settings UI artwork

These are the explicitly approved interface images, embedded by the fixed
allowlist in `tools/embed_ui_images.py`. No game assets or ROM data are included.

- `icon.png`: the existing original project egg from `tools/gen_icon.py`.
- `controller-xbox.svg`: supplied by TK22-26 as `controller-xbox-flat.svg`.
- `controller-playstation.svg`: the static diagram from TK22-26's supplied
  `Component · DualSense diagram (cleaned from your trace)-html/PSArt.dc.html`.
  Inactive highlight overlays and HTML click handlers were removed. Shape
  geometry is retained. Both controller diagrams use the settings theme's
  dark greens, soft green controls, and cream details.
- `github-mark-white.svg`: unmodified `GitHub_Invertocat_White.svg` from
  [GitHub's official logo download](https://brand.github.com/GitHub_Logos.zip).
  Displayed only beside links to the projects on GitHub, per the
  [GitHub logo policy](https://docs.github.com/en/site-policy/other-site-policies/github-logo-policy).
  GitHub's mark remains its trademark; its inclusion does not imply endorsement.

SVGs are rasterized for the current monitor DPI by the vendored NanoSVG parser
and rasterizer. Fonts are loaded from the operating system.
