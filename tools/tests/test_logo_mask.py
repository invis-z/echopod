import sys
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import logo_mask


def svg_of(*paths):
    body = "".join(f'<path d="{path}"/>' for path in paths)
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 160 160">{body}</svg>'


def at(levels, side, x, y):
    return levels[y * side + x]


class PathTest(unittest.TestCase):
    def test_numbers_run_together_as_svg_allows(self):
        shapes = logo_mask.subpaths("M1.5.5l-1-.5h2v1z")
        self.assertEqual((1.5, 0.5), shapes[0][0])
        self.assertEqual((0.5, 0.0), shapes[0][1])
        self.assertEqual((2.5, 0.0), shapes[0][2])
        self.assertEqual((2.5, 1.0), shapes[0][3])

    def test_relative_moves_follow_the_pen_and_close_returns_to_the_start(self):
        shapes = logo_mask.subpaths("M10,10 h20 v20 h-20 z m40,0 h10 v10 z")
        self.assertEqual(2, len(shapes))
        self.assertEqual((10.0, 10.0), shapes[0][0])
        self.assertEqual((10.0, 30.0), shapes[0][-1])
        self.assertEqual((50.0, 10.0), shapes[1][0])

    def test_a_smooth_curve_mirrors_the_last_control_point(self):
        explicit = logo_mask.subpaths("M0,0 C0,10 10,10 10,0 C10,-10 20,-10 20,0")
        smooth = logo_mask.subpaths("M0,0 C0,10 10,10 10,0 S20,-10 20,0")
        self.assertEqual(explicit, smooth)

    def test_a_smooth_quadratic_mirrors_the_last_control_point(self):
        explicit = logo_mask.subpaths("M0,0 Q5,10 10,0 Q15,-10 20,0")
        smooth = logo_mask.subpaths("M0,0 Q5,10 10,0 T20,0")
        self.assertEqual(explicit, smooth)

    def test_an_unknown_command_is_refused(self):
        with self.assertRaises(ValueError):
            logo_mask.subpaths("M0,0 A5,5 0 0 1 10,10")


class CoverageTest(unittest.TestCase):
    def test_a_square_is_solid_inside_and_empty_outside(self):
        levels = logo_mask.coverage(svg_of("M40,40 H120 V120 H40 Z"), 16)
        self.assertEqual(logo_mask.LEVELS, at(levels, 16, 8, 8))
        self.assertEqual(0, at(levels, 16, 1, 1))

    def test_a_square_inside_a_square_in_the_same_path_is_a_hole(self):
        levels = logo_mask.coverage(svg_of("M20,20 H140 V140 H20 Z M60,60 H100 V100 H60 Z"), 16)
        self.assertEqual(0, at(levels, 16, 8, 8))
        self.assertEqual(logo_mask.LEVELS, at(levels, 16, 3, 8))

    def test_separate_paths_add_up(self):
        levels = logo_mask.coverage(svg_of("M0,0 H80 V160 H0 Z", "M80,0 H160 V160 H80 Z"), 16)
        self.assertEqual([logo_mask.LEVELS] * 256, levels)

    def test_a_half_covered_edge_pixel_is_half_on(self):
        levels = logo_mask.coverage(svg_of("M0,0 H85 V160 H0 Z"), 16)
        self.assertEqual(8, at(levels, 16, 8, 8))

    def test_the_rippl_logo_has_a_solid_head_and_an_empty_corner(self):
        svg = (TOOLS_DIRECTORY.parent / "assets" / "rippl.svg").read_text()
        levels = logo_mask.coverage(svg, 80)
        self.assertEqual(logo_mask.LEVELS, at(levels, 80, 40, 66))
        self.assertEqual(0, at(levels, 80, 0, 0))


class PackingTest(unittest.TestCase):
    def test_the_first_pixel_takes_the_low_nibble(self):
        self.assertEqual([0x21, 0x03], logo_mask.packed([1, 2, 3]))


if __name__ == "__main__":
    unittest.main()
