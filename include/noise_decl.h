/* A noise algorithm that wraps well around data type limits.
 *
 * LICENCE: CC BY-NC-SA 4.0
 * For commercial use, contact me at q@qnixsoft.com
 *
 * DEFINITIONS:
 *
 * a point has DIM coordinates
 *
 * a matrix has 2^y (l) edge length.
 * It is an array of uint32_t where we want to store the result.
 *
 * A noise feature quad has 2^x (d) edge length
 * and 2^DIM vertices with random values.
 *
 * Other values are the result of a "fade" between those.
 * And they are added to the resulting matrix (for multiple octaves);
 *
 * v is an array that stores a (noise feature) quad's vertices.
 * It has a peculiar order FIXME
 *
 * recursive implementations are here for reference
 * TODO further optimizations
 * TODO does this work in 3D+?
 * TODO improve documentation
 * TODO speed tests
 * TODO think before changing stuff
 * */

#ifndef NOISE_H
#define NOISE_H

#include <stdint.h>
#include <stddef.h>

#define NOISE_MAX ((uint32_t) -1)

typedef struct { unsigned x, w; } octave_t;

void
noise_oct(uint32_t *m, int16_t *s, size_t oct_n,
	  octave_t *oct, unsigned seed, unsigned cy, uint8_t dim);

#endif /* NOISE_H */
