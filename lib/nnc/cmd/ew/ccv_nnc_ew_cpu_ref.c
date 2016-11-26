#include <ccv.h>
#include <ccv_internal.h>
#include <nnc/ccv_nnc.h>
#include <nnc/ccv_nnc_easy.h>
#include <nnc/ccv_nnc_internal.h>
#ifdef USE_OPENMP
#include <omp.h>
#endif
#ifdef USE_DISPATCH
#include <dispatch/dispatch.h>
#endif

#include "../_ccv_nnc_cpu_ref.h"

int _ccv_nnc_ewsum_forw_cpu_ref(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	if (input_size == 1 && output_size == 1)
	{
		_ccv_nnc_tensor_transfer_cpu_ref((const ccv_nnc_tensor_view_t*)inputs[0], (ccv_nnc_tensor_view_t*)outputs[0]);
		return CCV_NNC_EXEC_SUCCESS;
	}
	// Assuming this is float 32.
	int dim[CCV_NNC_MAX_DIM + 2];
	int ainc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	int cinc[CCV_NNC_MAX_DIM + 2];
	int x, z;
	int k = 0;
	// Bad, I promised this can be inplace operation. Need to first find out if there are share the same pointer first.
	for (z = 1; z < input_size; z++)
	{
		ccv_nnc_tensor_view_t* c = (ccv_nnc_tensor_view_t*)outputs[0];
		ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[z];
		if (c->data.f32 == a->data.f32)
		{
			k = z;
			break;
		}
	}
	for (z = 0; z < input_size - 1; z++)
	{
		ccv_nnc_tensor_view_t* c = (ccv_nnc_tensor_view_t*)outputs[0];
		ccv_nnc_tensor_view_t* a = z > 0 ? c : (ccv_nnc_tensor_view_t*)inputs[k];
		ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)(z >= k ? inputs[z + 1] : inputs[z]);
		assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(c->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, b->info.dim[x]));
			assert(ccv_max(1, b->info.dim[x]) == ccv_max(1, c->info.dim[x]));
			dim[x] = ccv_max(1, a->info.dim[x]);
			ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
			cinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(c) ? c->inc[x] : c->info.dim[x]);
		}
		if (!CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(c))
		{
			// Super optimal case, just do one for-loop for sum.
			const int tensor_count = ccv_nnc_tensor_count(a->info);
			for (x = 0; x < tensor_count; x++)
				c->data.f32[x] = a->data.f32[x] + b->data.f32[x];
			continue;
		}
		assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
		int i[CCV_NNC_MAX_DIM + 2];
		float* ap = a->data.f32;
		float* bp = b->data.f32;
		float* cp = c->data.f32;
		const int count = dim[1] * dim[0];
		if (ainc[0] == dim[0] && binc[0] == dim[0] && cinc[0] == dim[0])
		{
			// Special casing if the ainc[0] is the same as dim[0] (do memcpy for the last two dim)
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (x = 0; x < count; x++)
						cp[x] = ap[x] + bp[x];
					ap += ainc[1] * ainc[0];
					bp += binc[1] * binc[0];
					cp += cinc[1] * cinc[0];
				}
				ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
			}
			continue;
		}
		// Non-optimal case, need to do skip copy.
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (i[1] = 0; i[1] < dim[1]; i[1]++)
				{
					for (x = 0; x < dim[0]; x++)
						cp[x] = ap[x] + bp[x];
					ap += ainc[0];
					bp += binc[0];
					cp += cinc[0];
				}
				ap += (ainc[1] - dim[1]) * ainc[0];
				bp += (binc[1] - dim[1]) * binc[0];
				cp += (cinc[1] - dim[1]) * cinc[0];
			}
			ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
			cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
		}
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewsum_back(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// D[x + y + z, x] = 1
	int i;
	if (inputs[0] == 0)
		// Set them to 1.
		for (i = 0; i < output_size; i++)
			_ccv_nnc_tensor_set_cpu_ref((ccv_nnc_tensor_view_t*)outputs[i], 1);
	else
		// Copy over the gradient
		for (i = 0; i < output_size; i++)
			_ccv_nnc_tensor_transfer_cpu_ref((ccv_nnc_tensor_view_t*)inputs[0], (ccv_nnc_tensor_view_t*)outputs[i]);
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewprod_forw(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	if (input_size == 1 && output_size == 1)
	{
		_ccv_nnc_tensor_transfer_cpu_ref((const ccv_nnc_tensor_view_t*)inputs[0], (ccv_nnc_tensor_view_t*)outputs[0]);
		return CCV_NNC_EXEC_SUCCESS;
	}
	// Assuming this is float 32.
	int dim[CCV_NNC_MAX_DIM + 2];
	int ainc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	int cinc[CCV_NNC_MAX_DIM + 2];
	int x, z;
	int k = 0;
	// Bad, I promised this can be inplace operation. Need to first find out if there are share the same pointer first.
	for (z = 1; z < input_size; z++)
	{
		ccv_nnc_tensor_view_t* c = (ccv_nnc_tensor_view_t*)outputs[0];
		ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[z];
		if (c->data.f32 == a->data.f32)
		{
			k = z;
			break;
		}
	}
	for (z = 0; z < input_size - 1; z++)
	{
		ccv_nnc_tensor_view_t* c = (ccv_nnc_tensor_view_t*)outputs[0];
		ccv_nnc_tensor_view_t* a = z > 0 ? c : (ccv_nnc_tensor_view_t*)inputs[k];
		ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)(z >= k ? inputs[z + 1] : inputs[z]);
		assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(c->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, b->info.dim[x]));
			assert(ccv_max(1, b->info.dim[x]) == ccv_max(1, c->info.dim[x]));
			dim[x] = ccv_max(1, a->info.dim[x]);
			ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
			cinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(c) ? c->inc[x] : c->info.dim[x]);
		}
		if (!CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(c))
		{
			// Super optimal case, just do one for-loop for sum.
			const int tensor_count = ccv_nnc_tensor_count(a->info);
			for (x = 0; x < tensor_count; x++)
				c->data.f32[x] = a->data.f32[x] * b->data.f32[x];
			continue;
		}
		assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
		int i[CCV_NNC_MAX_DIM + 2];
		float* ap = a->data.f32;
		float* bp = b->data.f32;
		float* cp = c->data.f32;
		const int count = dim[1] * dim[0];
		if (ainc[0] == dim[0] && binc[0] == dim[0] && cinc[0] == dim[0])
		{
			// Special casing if the ainc[0] is the same as dim[0]
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (x = 0; x < count; x++)
						cp[x] = ap[x] * bp[x];
					ap += ainc[1] * ainc[0];
					bp += binc[1] * binc[0];
					cp += cinc[1] * cinc[0];
				}
				ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
			}
			continue;
		}
		// Non-optimal case, need to do skip copy.
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (i[1] = 0; i[1] < dim[1]; i[1]++)
				{
					for (x = 0; x < dim[0]; x++)
						cp[x] = ap[x] * bp[x];
					ap += ainc[0];
					bp += binc[0];
					cp += cinc[0];
				}
				ap += (ainc[1] - dim[1]) * ainc[0];
				bp += (binc[1] - dim[1]) * binc[0];
				cp += (cinc[1] - dim[1]) * cinc[0];
			}
			ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
			cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
		}
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewprod_back(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// D[x * y * z, x] = y * z
	// Assuming this is float 32.
	int dim[CCV_NNC_MAX_DIM + 2];
	int ginc[CCV_NNC_MAX_DIM + 2];
	int ainc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	int hinc[CCV_NNC_MAX_DIM + 2];
	int x, z;
	ccv_nnc_tensor_view_t* g = (ccv_nnc_tensor_view_t*)inputs[0];
	ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)inputs[output_size + 1];
	if (g == 0)
	{
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			dim[x] = ccv_max(1, b->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
		}
		for (z = 0; z < output_size; z++)
		{
			ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[z + 1];
			ccv_nnc_tensor_view_t* h = (ccv_nnc_tensor_view_t*)outputs[z];
			assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
			assert(h->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
			for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
			{
				assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, h->info.dim[x]));
				assert(ccv_max(1, h->info.dim[x]) == ccv_max(1, b->info.dim[x]));
				ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
				hinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(h) ? h->inc[x] : h->info.dim[x]);
			}
			if (!CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(h))
			{
				// Super optimal case, just do one for-loop for sum.
				const int tensor_count = ccv_nnc_tensor_count(b->info);
				for (x = 0; x < tensor_count; x++)
					h->data.f32[x] = b->data.f32[x] / a->data.f32[x];
				continue;
			}
			assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
			int i[CCV_NNC_MAX_DIM + 2];
			float* ap = a->data.f32;
			float* bp = b->data.f32;
			float* hp = h->data.f32;
			const int count = dim[1] * dim[0];
			if (ainc[0] == dim[0] && binc[0] == dim[0] && hinc[0] == dim[0])
			{
				// Special casing if the ainc[0] is the same as dim[0]
				for (i[3] = 0; i[3] < dim[3]; i[3]++)
				{
					for (i[2] = 0; i[2] < dim[2]; i[2]++)
					{
						for (x = 0; x < count; x++)
							hp[x] = bp[x] / ap[x];
						ap += ainc[1] * ainc[0];
						bp += binc[1] * binc[0];
						hp += hinc[1] * hinc[0];
					}
					ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
					bp += (binc[2] - dim[2]) * binc[1] * binc[0];
					hp += (hinc[2] - dim[2]) * hinc[1] * hinc[0];
				}
				continue;
			}
			// Non-optimal case, need to do skip copy.
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (i[1] = 0; i[1] < dim[1]; i[1]++)
					{
						for (x = 0; x < dim[0]; x++)
							hp[x] = bp[x] / ap[x];
						ap += ainc[0];
						bp += binc[0];
						hp += hinc[0];
					}
					ap += (ainc[1] - dim[1]) * ainc[0];
					bp += (binc[1] - dim[1]) * binc[0];
					hp += (hinc[1] - dim[1]) * hinc[0];
				}
				ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				hp += (hinc[2] - dim[2]) * hinc[1] * hinc[0];
			}
		}
	} else {
		assert(g->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			dim[x] = ccv_max(1, b->info.dim[x]);
			ginc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(g) ? g->inc[x] : g->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
		}
		for (z = 0; z < output_size; z++)
		{
			ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[z + 1];
			ccv_nnc_tensor_view_t* h = (ccv_nnc_tensor_view_t*)outputs[z];
			assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
			assert(h->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
			for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
			{
				assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, h->info.dim[x]));
				assert(ccv_max(1, h->info.dim[x]) == ccv_max(1, g->info.dim[x]));
				assert(ccv_max(1, g->info.dim[x]) == ccv_max(1, b->info.dim[x]));
				ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
				hinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(h) ? h->inc[x] : h->info.dim[x]);
			}
			if (!CCV_IS_TENSOR_VIEW(g) && !CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(h))
			{
				// Super optimal case, just do one for-loop for sum.
				const int tensor_count = ccv_nnc_tensor_count(g->info);
				for (x = 0; x < tensor_count; x++)
					h->data.f32[x] = g->data.f32[x] * b->data.f32[x] / a->data.f32[x];
				continue;
			}
			assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
			int i[CCV_NNC_MAX_DIM + 2];
			float* gp = g->data.f32;
			float* ap = a->data.f32;
			float* bp = b->data.f32;
			float* hp = h->data.f32;
			const int count = dim[1] * dim[0];
			if (ginc[0] == dim[0] && ainc[0] == dim[0] && binc[0] == dim[0] && hinc[0] == dim[0])
			{
				// Special casing if the ainc[0] is the same as dim[0]
				for (i[3] = 0; i[3] < dim[3]; i[3]++)
				{
					for (i[2] = 0; i[2] < dim[2]; i[2]++)
					{
						for (x = 0; x < count; x++)
							hp[x] = gp[x] * bp[x] / ap[x];
						gp += ginc[1] * ginc[0];
						ap += ainc[1] * ainc[0];
						bp += binc[1] * binc[0];
						hp += hinc[1] * hinc[0];
					}
					gp += (ginc[2] - dim[2]) * ginc[1] * ginc[0];
					ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
					bp += (binc[2] - dim[2]) * binc[1] * binc[0];
					hp += (hinc[2] - dim[2]) * hinc[1] * hinc[0];
				}
				continue;
			}
			// Non-optimal case, need to do skip copy.
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (i[1] = 0; i[1] < dim[1]; i[1]++)
					{
						for (x = 0; x < dim[0]; x++)
							hp[x] = gp[x] * bp[x] / ap[x];
						gp += ginc[0];
						ap += ainc[0];
						bp += binc[0];
						hp += hinc[0];
					}
					gp += (ginc[1] - dim[1]) * ginc[0];
					ap += (ainc[1] - dim[1]) * ainc[0];
					bp += (binc[1] - dim[1]) * binc[0];
					hp += (hinc[1] - dim[1]) * hinc[0];
				}
				gp += (ginc[2] - dim[2]) * ginc[1] * ginc[0];
				ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				hp += (hinc[2] - dim[2]) * hinc[1] * hinc[0];
			}
		}
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewdiv_forw(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// Assuming this is float 32.
	int dim[CCV_NNC_MAX_DIM + 2];
	int ainc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	int cinc[CCV_NNC_MAX_DIM + 2];
	ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[0];
	ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)inputs[1];
	ccv_nnc_tensor_view_t* c = (ccv_nnc_tensor_view_t*)outputs[0];
	if (a == 0) // Take 0 as all ones tensor.
	{
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(c->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		int x;
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			assert(ccv_max(1, b->info.dim[x]) == ccv_max(1, c->info.dim[x]));
			dim[x] = ccv_max(1, b->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
			cinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(c) ? c->inc[x] : c->info.dim[x]);
		}
		if (!CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(c))
		{
			// Super optimal case, just do one for-loop for sum.
			const int tensor_count = ccv_nnc_tensor_count(b->info);
			for (x = 0; x < tensor_count; x++)
				c->data.f32[x] = 1 / b->data.f32[x];
			return CCV_NNC_EXEC_SUCCESS;
		}
		assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
		int i[CCV_NNC_MAX_DIM + 2];
		float* bp = b->data.f32;
		float* cp = c->data.f32;
		const int count = dim[1] * dim[0];
		if (binc[0] == dim[0] && cinc[0] == dim[0])
		{
			// Special casing if the ainc[0] is the same as dim[0]
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (x = 0; x < count; x++)
						cp[x] = 1 / bp[x];
					bp += binc[1] * binc[0];
					cp += cinc[1] * cinc[0];
				}
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
			}
			return CCV_NNC_EXEC_SUCCESS;
		}
		// Non-optimal case, need to do skip copy.
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (i[1] = 0; i[1] < dim[1]; i[1]++)
				{
					for (x = 0; x < dim[0]; x++)
						cp[x] = 1 / bp[x];
					bp += binc[0];
					cp += cinc[0];
				}
				bp += (binc[1] - dim[1]) * binc[0];
				cp += (cinc[1] - dim[1]) * cinc[0];
			}
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
			cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
		}
	} else {
		assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(c->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		int x;
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, b->info.dim[x]));
			assert(ccv_max(1, b->info.dim[x]) == ccv_max(1, c->info.dim[x]));
			dim[x] = ccv_max(1, a->info.dim[x]);
			ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
			cinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(c) ? c->inc[x] : c->info.dim[x]);
		}
		if (!CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(c))
		{
			// Super optimal case, just do one for-loop for sum.
			const int tensor_count = ccv_nnc_tensor_count(a->info);
			for (x = 0; x < tensor_count; x++)
				c->data.f32[x] = a->data.f32[x] / b->data.f32[x];
			return CCV_NNC_EXEC_SUCCESS;
		}
		assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
		int i[CCV_NNC_MAX_DIM + 2];
		float* ap = a->data.f32;
		float* bp = b->data.f32;
		float* cp = c->data.f32;
		const int count = dim[1] * dim[0];
		if (ainc[0] == dim[0] && binc[0] == dim[0] && cinc[0] == dim[0])
		{
			// Special casing if the ainc[0] is the same as dim[0]
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (x = 0; x < count; x++)
						cp[x] = ap[x] / bp[x];
					ap += ainc[1] * ainc[0];
					bp += binc[1] * binc[0];
					cp += cinc[1] * cinc[0];
				}
				ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
			}
			return CCV_NNC_EXEC_SUCCESS;
		}
		// Non-optimal case, need to do skip copy.
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (i[1] = 0; i[1] < dim[1]; i[1]++)
				{
					for (x = 0; x < dim[0]; x++)
						cp[x] = ap[x] / bp[x];
					ap += ainc[0];
					bp += binc[0];
					cp += cinc[0];
				}
				ap += (ainc[1] - dim[1]) * ainc[0];
				bp += (binc[1] - dim[1]) * binc[0];
				cp += (cinc[1] - dim[1]) * cinc[0];
			}
			ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
			cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
		}
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewdiv_back(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// D[x / y, x] = 1 / y, D[x / y, y] = -x / y^2
	if (output_size == 1 || outputs[1] == 0)
	{
		// When we only need D[x / y, y]
		ccv_nnc_cmd_t forw_cmd = cmd;
		forw_cmd.cmd = CCV_NNC_EWDIV_FORWARD;
		return _ccv_nnc_ewdiv_forw(cmd, ccv_nnc_no_hint, flags, TENSOR_LIST(inputs[0], inputs[2]), &outputs[0], 1, stream_context);
	}
	int dim[CCV_NNC_MAX_DIM + 2];
	int ginc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	int cinc[CCV_NNC_MAX_DIM + 2];
	int hainc[CCV_NNC_MAX_DIM + 2];
	int hbinc[CCV_NNC_MAX_DIM + 2];
	ccv_nnc_tensor_view_t* g = (ccv_nnc_tensor_view_t*)inputs[0];
	ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)inputs[2];
	ccv_nnc_tensor_view_t* c = (ccv_nnc_tensor_view_t*)inputs[3];
	ccv_nnc_tensor_view_t* ha = (ccv_nnc_tensor_view_t*)outputs[0];
	ccv_nnc_tensor_view_t* hb = (ccv_nnc_tensor_view_t*)outputs[1];
	if (g == 0)
	{
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(c->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(ha->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(hb->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		int x;
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			assert(ccv_max(1, b->info.dim[x]) == ccv_max(1, c->info.dim[x]));
			assert(ccv_max(1, c->info.dim[x]) == ccv_max(1, ha->info.dim[x]));
			assert(ccv_max(1, ha->info.dim[x]) == ccv_max(1, hb->info.dim[x]));
			dim[x] = ccv_max(1, b->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
			cinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(c) ? c->inc[x] : c->info.dim[x]);
			hainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? ha->inc[x] : ha->info.dim[x]);
			hbinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(hb) ? hb->inc[x] : hb->info.dim[x]);
		}
		if (!CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(c) && !CCV_IS_TENSOR_VIEW(ha) && !CCV_IS_TENSOR_VIEW(hb))
		{
			// Super optimal case, just do one for-loop for sum.
			const int tensor_count = ccv_nnc_tensor_count(b->info);
			for (x = 0; x < tensor_count; x++)
			{
				const float v = 1 / b->data.f32[x];
				ha->data.f32[x] = v;
				hb->data.f32[x] = -c->data.f32[x] * v;
			}
			return CCV_NNC_EXEC_SUCCESS;
		}
		assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
		int i[CCV_NNC_MAX_DIM + 2];
		float* bp = b->data.f32;
		float* cp = c->data.f32;
		float* hap = ha->data.f32;
		float* hbp = hb->data.f32;
		const int count = dim[1] * dim[0];
		if (binc[0] == dim[0] && cinc[0] == dim[0] && hainc[0] == dim[0] && hbinc[0] == dim[0])
		{
			// Special casing if the ainc[0] is the same as dim[0]
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (x = 0; x < count; x++)
					{
						const float v = 1 / bp[x];
						hap[x] = v;
						hbp[x] = -cp[x] * v;
					}
					bp += binc[1] * binc[0];
					cp += cinc[1] * cinc[0];
					hap += hainc[1] * hainc[0];
					hbp += hbinc[1] * hbinc[0];
				}
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
				hap += (hainc[2] - dim[2]) * hainc[1] * hainc[0];
				hbp += (hbinc[2] - dim[2]) * hbinc[1] * hbinc[0];
			}
			return CCV_NNC_EXEC_SUCCESS;
		}
		// Non-optimal case, need to do skip copy.
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (i[1] = 0; i[1] < dim[1]; i[1]++)
				{
					for (x = 0; x < dim[0]; x++)
					{
						const float v = 1 / bp[x];
						hap[x] = v;
						hbp[x] = -cp[x] * v;
					}
					bp += binc[0];
					cp += cinc[0];
					hap += hainc[0];
					hbp += hbinc[0];
				}
				bp += (binc[1] - dim[1]) * binc[0];
				cp += (cinc[1] - dim[1]) * cinc[0];
				hap += (hainc[1] - dim[1]) * hainc[0];
				hbp += (hbinc[1] - dim[1]) * hbinc[0];
			}
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
			cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
			hap += (hainc[2] - dim[2]) * hainc[1] * hainc[0];
			hbp += (hbinc[2] - dim[2]) * hbinc[1] * hbinc[0];
		}
	} else {
		assert(g->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(c->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(ha->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		assert(hb->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
		int x;
		for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
		{
			assert(ccv_max(1, g->info.dim[x]) == ccv_max(1, g->info.dim[x]));
			assert(ccv_max(1, b->info.dim[x]) == ccv_max(1, c->info.dim[x]));
			assert(ccv_max(1, c->info.dim[x]) == ccv_max(1, ha->info.dim[x]));
			assert(ccv_max(1, ha->info.dim[x]) == ccv_max(1, hb->info.dim[x]));
			dim[x] = ccv_max(1, b->info.dim[x]);
			ginc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(g) ? g->inc[x] : g->info.dim[x]);
			binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
			cinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(c) ? c->inc[x] : c->info.dim[x]);
			hainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? ha->inc[x] : ha->info.dim[x]);
			hbinc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(hb) ? hb->inc[x] : hb->info.dim[x]);
		}
		if (!CCV_IS_TENSOR_VIEW(g) && !CCV_IS_TENSOR_VIEW(b) && !CCV_IS_TENSOR_VIEW(c) && !CCV_IS_TENSOR_VIEW(ha) && !CCV_IS_TENSOR_VIEW(hb))
		{
			// Super optimal case, just do one for-loop for sum.
			const int tensor_count = ccv_nnc_tensor_count(g->info);
			for (x = 0; x < tensor_count; x++)
			{
				const float v = g->data.f32[x] / b->data.f32[x];
				ha->data.f32[x] = v;
				hb->data.f32[x] = -c->data.f32[x] * v;
			}
			return CCV_NNC_EXEC_SUCCESS;
		}
		assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
		int i[CCV_NNC_MAX_DIM + 2];
		float* gp = g->data.f32;
		float* bp = b->data.f32;
		float* cp = c->data.f32;
		float* hap = ha->data.f32;
		float* hbp = hb->data.f32;
		const int count = dim[1] * dim[0];
		if (ginc[0] == dim[0] && binc[0] == dim[0] && cinc[0] == dim[0] && hainc[0] == dim[0] && hbinc[0] == dim[0])
		{
			// Special casing if the ainc[0] is the same as dim[0]
			for (i[3] = 0; i[3] < dim[3]; i[3]++)
			{
				for (i[2] = 0; i[2] < dim[2]; i[2]++)
				{
					for (x = 0; x < count; x++)
					{
						const float v = gp[x] / bp[x];
						hap[x] = v;
						hbp[x] = -cp[x] * v;
					}
					gp += ginc[1] * ginc[0];
					bp += binc[1] * binc[0];
					cp += cinc[1] * cinc[0];
					hap += hainc[1] * hainc[0];
					hbp += hbinc[1] * hbinc[0];
				}
				gp += (ginc[2] - dim[2]) * ginc[1] * ginc[0];
				bp += (binc[2] - dim[2]) * binc[1] * binc[0];
				cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
				hap += (hainc[2] - dim[2]) * hainc[1] * hainc[0];
				hbp += (hbinc[2] - dim[2]) * hbinc[1] * hbinc[0];
			}
			return CCV_NNC_EXEC_SUCCESS;
		}
		// Non-optimal case, need to do skip copy.
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (i[1] = 0; i[1] < dim[1]; i[1]++)
				{
					for (x = 0; x < dim[0]; x++)
					{
						const float v = gp[x] / bp[x];
						hap[x] = v;
						hbp[x] = -cp[x] * v;
					}
					gp += ginc[0];
					bp += binc[0];
					cp += cinc[0];
					hap += hainc[0];
					hbp += hbinc[0];
				}
				gp += (ginc[1] - dim[1]) * ginc[0];
				bp += (binc[1] - dim[1]) * binc[0];
				cp += (cinc[1] - dim[1]) * cinc[0];
				hap += (hainc[1] - dim[1]) * hainc[0];
				hbp += (hbinc[1] - dim[1]) * hbinc[0];
			}
			gp += (ginc[2] - dim[2]) * ginc[1] * ginc[0];
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
			cp += (cinc[2] - dim[2]) * cinc[1] * cinc[0];
			hap += (hainc[2] - dim[2]) * hainc[1] * hainc[0];
			hbp += (hbinc[2] - dim[2]) * hbinc[1] * hbinc[0];
		}
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewexp_forw(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// Assuming this is float 32.
	int dim[CCV_NNC_MAX_DIM + 2];
	int ainc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[0];
	ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)outputs[0];
	assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
	assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
	int x;
	for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
	{
		assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, b->info.dim[x]));
		dim[x] = ccv_max(1, a->info.dim[x]);
		ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
		binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
	}
	if (!CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b))
	{
		// Super optimal case, just do one for-loop for sum.
		const int tensor_count = ccv_nnc_tensor_count(a->info);
		for (x = 0; x < tensor_count; x++)
			b->data.f32[x] = exp(a->data.f32[x]);
		return CCV_NNC_EXEC_SUCCESS;
	}
	assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
	int i[CCV_NNC_MAX_DIM + 2];
	float* ap = a->data.f32;
	float* bp = b->data.f32;
	const int count = dim[1] * dim[0];
	if (ainc[0] == dim[0] && binc[0] == dim[0])
	{
		// Special casing if the ainc[0] is the same as dim[0]
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (x = 0; x < count; x++)
					bp[x] = exp(ap[x]);
				ap += ainc[1] * ainc[0];
				bp += binc[1] * binc[0];
			}
			ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
		}
		return CCV_NNC_EXEC_SUCCESS;
	}
	// Non-optimal case, need to do skip copy.
	for (i[3] = 0; i[3] < dim[3]; i[3]++)
	{
		for (i[2] = 0; i[2] < dim[2]; i[2]++)
		{
			for (i[1] = 0; i[1] < dim[1]; i[1]++)
			{
				for (x = 0; x < dim[0]; x++)
					bp[x] = exp(ap[x]);
				ap += ainc[0];
				bp += binc[0];
			}
			ap += (ainc[1] - dim[1]) * ainc[0];
			bp += (binc[1] - dim[1]) * binc[0];
		}
		ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
		bp += (binc[2] - dim[2]) * binc[1] * binc[0];
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewexp_back(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// D[Exp[x], x] = Exp[x]
	if (inputs[0] == 0)
	{
		_ccv_nnc_tensor_transfer_cpu_ref((ccv_nnc_tensor_view_t*)inputs[2], (ccv_nnc_tensor_view_t*)outputs[0]);
		return CCV_NNC_EXEC_SUCCESS;
	} else {
		ccv_nnc_cmd_t forw_cmd = cmd;
		forw_cmd.cmd = CCV_NNC_EWPROD_FORWARD;
		return _ccv_nnc_ewprod_forw(cmd, ccv_nnc_no_hint, flags, TENSOR_LIST(inputs[0], inputs[2]), outputs, output_size, stream_context);
	}
}

static int _ccv_nnc_ewlog_forw(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	// Assuming this is float 32.
	int dim[CCV_NNC_MAX_DIM + 2];
	int ainc[CCV_NNC_MAX_DIM + 2];
	int binc[CCV_NNC_MAX_DIM + 2];
	ccv_nnc_tensor_view_t* a = (ccv_nnc_tensor_view_t*)inputs[0];
	ccv_nnc_tensor_view_t* b = (ccv_nnc_tensor_view_t*)outputs[0];
	assert(a->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
	assert(b->info.dim[CCV_NNC_MAX_DIM + 2] == 0);
	int x;
	for (x = 0; x < CCV_NNC_MAX_DIM + 2; x++)
	{
		assert(ccv_max(1, a->info.dim[x]) == ccv_max(1, b->info.dim[x]));
		dim[x] = ccv_max(1, a->info.dim[x]);
		ainc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(a) ? a->inc[x] : a->info.dim[x]);
		binc[x] = ccv_max(1, CCV_IS_TENSOR_VIEW(b) ? b->inc[x] : b->info.dim[x]);
	}
	if (!CCV_IS_TENSOR_VIEW(a) && !CCV_IS_TENSOR_VIEW(b))
	{
		// Super optimal case, just do one for-loop for sum.
		const int tensor_count = ccv_nnc_tensor_count(a->info);
		for (x = 0; x < tensor_count; x++)
			b->data.f32[x] = log(a->data.f32[x]);
		return CCV_NNC_EXEC_SUCCESS;
	}
	assert(CCV_NNC_MAX_DIM == 2); // Need to change this logic for CCV_NNC_MAX_DIM == other number.
	int i[CCV_NNC_MAX_DIM + 2];
	float* ap = a->data.f32;
	float* bp = b->data.f32;
	const int count = dim[1] * dim[0];
	if (ainc[0] == dim[0] && binc[0] == dim[0])
	{
		// Special casing if the ainc[0] is the same as dim[0]
		for (i[3] = 0; i[3] < dim[3]; i[3]++)
		{
			for (i[2] = 0; i[2] < dim[2]; i[2]++)
			{
				for (x = 0; x < count; x++)
					bp[x] = log(ap[x]);
				ap += ainc[1] * ainc[0];
				bp += binc[1] * binc[0];
			}
			ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
			bp += (binc[2] - dim[2]) * binc[1] * binc[0];
		}
		return CCV_NNC_EXEC_SUCCESS;
	}
	// Non-optimal case, need to do skip copy.
	for (i[3] = 0; i[3] < dim[3]; i[3]++)
	{
		for (i[2] = 0; i[2] < dim[2]; i[2]++)
		{
			for (i[1] = 0; i[1] < dim[1]; i[1]++)
			{
				for (x = 0; x < dim[0]; x++)
					bp[x] = log(ap[x]);
				ap += ainc[0];
				bp += binc[0];
			}
			ap += (ainc[1] - dim[1]) * ainc[0];
			bp += (binc[1] - dim[1]) * binc[0];
		}
		ap += (ainc[2] - dim[2]) * ainc[1] * ainc[0];
		bp += (binc[2] - dim[2]) * binc[1] * binc[0];
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static int _ccv_nnc_ewlog_back(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* inputs, const int input_size, ccv_nnc_tensor_t** outputs, const int output_size, const ccv_nnc_stream_context_t* stream_context)
{
	ccv_nnc_cmd_t forw_cmd = cmd;
	forw_cmd.cmd = CCV_NNC_EWDIV_FORWARD;
	// D[Log[x], x] = 1 / x
	return _ccv_nnc_ewdiv_forw(forw_cmd, ccv_nnc_no_hint, flags, TENSOR_LIST(inputs[0], inputs[1]), outputs, output_size, stream_context);
	// Otherwise, need to add them together.
	return CCV_NNC_EXEC_SUCCESS;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWSUM_FORWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewsum_forw_cpu_ref;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWSUM_BACKWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewsum_back;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWPROD_FORWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewprod_forw;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWPROD_BACKWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewprod_back;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWDIV_FORWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewdiv_forw;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWDIV_BACKWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewdiv_back;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWEXP_FORWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewexp_forw;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWEXP_BACKWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewexp_back;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWLOG_FORWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewlog_forw;
}

REGISTER_COMMAND_BACKEND(CCV_NNC_EWLOG_BACKWARD, CCV_NNC_BACKEND_CPU_REF)(ccv_nnc_cmd_backend_registry_t* registry)
{
	registry->tensor_formats = CCV_TENSOR_FORMAT_NHWC | CCV_TENSOR_FORMAT_NCHW | CCV_TENSOR_FORMAT_CHWN;
	registry->tensor_memory = CCV_TENSOR_CPU_MEMORY;
	registry->algorithms = 1;
	registry->exec = _ccv_nnc_ewlog_back;
}
