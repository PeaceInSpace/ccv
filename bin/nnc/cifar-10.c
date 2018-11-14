#include <ctype.h>
#include <ccv.h>
#include <ccv_internal.h>
#include <nnc/ccv_nnc.h>
#include <nnc/ccv_nnc_easy.h>
#include <3rdparty/dsfmt/dSFMT.h>
#include <sys/time.h>
#include <ctype.h>

static unsigned int get_current_time(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static ccv_cnnp_model_t* _building_block_new(const int filters, const int strides, const int border, const int projection_shortcut)
{
	ccv_cnnp_model_io_t input = ccv_cnnp_input();
	ccv_cnnp_model_io_t shortcut = input;
	ccv_cnnp_model_t* const identity = ccv_cnnp_identity((ccv_cnnp_param_t){
		.norm = CCV_CNNP_BATCH_NORM,
		.activation = CCV_CNNP_ACTIVATION_RELU,
	});
	ccv_cnnp_model_io_t output = ccv_cnnp_model_apply(identity, MODEL_IO_LIST(input));
	if (projection_shortcut)
	{
		ccv_cnnp_model_t* const conv0 = ccv_cnnp_convolution(1, filters, DIM_ALLOC(1, 1), (ccv_cnnp_param_t){
			.no_bias = 1,
			.hint = HINT((strides, strides), (0, 0)),
		});
		shortcut = ccv_cnnp_model_apply(conv0, MODEL_IO_LIST(output));
	}
	ccv_cnnp_model_t* const conv1 = ccv_cnnp_convolution(1, filters, DIM_ALLOC(3, 3), (ccv_cnnp_param_t){
		.norm = CCV_CNNP_BATCH_NORM,
		.activation = CCV_CNNP_ACTIVATION_RELU,
		.hint = HINT((strides, strides), (border, border)),
	});
	output = ccv_cnnp_model_apply(conv1, MODEL_IO_LIST(output));
	ccv_cnnp_model_t* const conv2 = ccv_cnnp_convolution(1, filters, DIM_ALLOC(3, 3), (ccv_cnnp_param_t){
		.no_bias = 1,
		.hint = HINT((1, 1), (1, 1)),
	});
	output = ccv_cnnp_model_apply(conv2, MODEL_IO_LIST(output));
	ccv_cnnp_model_t* const add = ccv_cnnp_add();
	output = ccv_cnnp_model_apply(add, MODEL_IO_LIST(output, shortcut));
	return ccv_cnnp_model_new(MODEL_IO_LIST(input), MODEL_IO_LIST(output));
}

static ccv_cnnp_model_t* _block_layer_new(const int filters, const int strides, const int border, const int blocks)
{
	ccv_cnnp_model_io_t input = ccv_cnnp_input();
	ccv_cnnp_model_t* first_block = _building_block_new(filters, strides, border, 1);
	ccv_cnnp_model_io_t output = ccv_cnnp_model_apply(first_block, MODEL_IO_LIST(input));
	int i;
	for (i = 1; i < blocks; i++)
	{
		ccv_cnnp_model_t* block = _building_block_new(filters, 1, 1, 0);
		output = ccv_cnnp_model_apply(block, MODEL_IO_LIST(output));
	}
	return ccv_cnnp_model_new(MODEL_IO_LIST(input), MODEL_IO_LIST(output));
}

ccv_cnnp_model_t* _cifar_10_resnet56(void)
{
	const ccv_cnnp_model_io_t input = ccv_cnnp_input();
	ccv_cnnp_model_t* init_conv = ccv_cnnp_convolution(1, 16, DIM_ALLOC(3, 3), (ccv_cnnp_param_t){
		.no_bias = 1,
		.hint = HINT((1, 1), (1, 1)),
	});
	ccv_cnnp_model_io_t output = ccv_cnnp_model_apply(init_conv, MODEL_IO_LIST(input));
	output = ccv_cnnp_model_apply(_block_layer_new(16, 1, 1, 9), MODEL_IO_LIST(output));
	output = ccv_cnnp_model_apply(_block_layer_new(32, 2, 1, 9), MODEL_IO_LIST(output));
	output = ccv_cnnp_model_apply(_block_layer_new(64, 2, 1, 9), MODEL_IO_LIST(output));
	ccv_cnnp_model_t* identity = ccv_cnnp_identity((ccv_cnnp_param_t){
		.norm = CCV_CNNP_BATCH_NORM,
		.activation = CCV_CNNP_ACTIVATION_RELU,
	});
	output = ccv_cnnp_model_apply(identity, MODEL_IO_LIST(output));
	output = ccv_cnnp_model_apply(ccv_cnnp_average_pool(DIM_ALLOC(0, 0), (ccv_cnnp_param_t){}), MODEL_IO_LIST(output));
	output = ccv_cnnp_model_apply(ccv_cnnp_flatten(), MODEL_IO_LIST(output));
	output = ccv_cnnp_model_apply(ccv_cnnp_dense(10, (ccv_cnnp_param_t){
		.activation = CCV_CNNP_ACTIVATION_SOFTMAX,
	}), MODEL_IO_LIST(output));
	return ccv_cnnp_model_new(MODEL_IO_LIST(input), MODEL_IO_LIST(output));
}

ccv_cnnp_model_t* _cifar_10_alexnet(void)
{
	return ccv_cnnp_sequential_new(MODEL_LIST(
		ccv_cnnp_convolution(1, 32, DIM_ALLOC(5, 5), (ccv_cnnp_param_t){
			.norm = CCV_CNNP_BATCH_NORM,
			.activation = CCV_CNNP_ACTIVATION_RELU,
			.hint = HINT((1, 1), (2, 2)),
		}),
		ccv_cnnp_max_pool(DIM_ALLOC(3, 3), (ccv_cnnp_param_t){
			.hint = HINT((2, 2), (0, 0)),
		}),
		ccv_cnnp_convolution(1, 32, DIM_ALLOC(5, 5), (ccv_cnnp_param_t){
			.norm = CCV_CNNP_BATCH_NORM,
			.activation = CCV_CNNP_ACTIVATION_RELU,
			.hint = HINT((1, 1), (2, 2)),
		}),
		ccv_cnnp_average_pool(DIM_ALLOC(3, 3), (ccv_cnnp_param_t){
			.hint = HINT((2, 2), (0, 0)),
		}),
		ccv_cnnp_convolution(1, 64, DIM_ALLOC(5, 5), (ccv_cnnp_param_t){
			.norm = CCV_CNNP_BATCH_NORM,
			.activation = CCV_CNNP_ACTIVATION_RELU,
			.hint = HINT((1, 1), (2, 2)),
		}),
		ccv_cnnp_average_pool(DIM_ALLOC(3, 3), (ccv_cnnp_param_t){
			.hint = HINT((2, 2), (0, 0)),
		}),
		ccv_cnnp_flatten(),
		ccv_cnnp_dense(256, (ccv_cnnp_param_t){
			.norm = CCV_CNNP_BATCH_NORM,
			.activation = CCV_CNNP_ACTIVATION_RELU,
		}),
		ccv_cnnp_dense(10, (ccv_cnnp_param_t){
			.activation = CCV_CNNP_ACTIVATION_SOFTMAX,
		})
	));
}

static void _array_categorized(const int column_idx, const int* row_idxs, const int row_size, void** const data, void* const context, ccv_nnc_stream_context_t* const stream_context)
{
	int i;
	ccv_array_t* const dataset = (ccv_array_t*)context;
	for (i = 0; i < row_size; i++)
		data[i] = (ccv_categorized_t*)ccv_array_get(dataset, row_idxs[i]);
}

typedef struct {
	ccv_nnc_tensor_t* input;
	ccv_nnc_tensor_t* fit;
} ccv_nnc_input_fit_t;

static void _input_fit_deinit(void* const self)
{
	ccv_nnc_input_fit_t* const input_fit = (ccv_nnc_input_fit_t*)self;
	ccv_nnc_tensor_free(input_fit->input);
	ccv_nnc_tensor_free(input_fit->fit);
	ccfree(input_fit);
}

typedef struct {
	dsfmt_t dsfmt;
	float mean[3];
	int batch_size;
	int device_count;
} ccv_nnc_reduce_context_t;

static void _reduce_train_batch_new(void** const input_data, const int batch_size, void** const output_data, void* const context, ccv_nnc_stream_context_t* const stream_context)
{
	ccv_nnc_reduce_context_t* const reduce_context = (ccv_nnc_reduce_context_t*)context;
	const int total_size = reduce_context->batch_size * reduce_context->device_count;
	if (!output_data[0])
	{
		ccv_nnc_input_fit_t* const input_fit = (ccv_nnc_input_fit_t*)(output_data[0] = ccmalloc(sizeof(ccv_nnc_input_fit_t)));
		input_fit->input = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(total_size, 3, 32, 32), 0);
		ccv_nnc_tensor_pin_memory(input_fit->input);
		input_fit->fit = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(total_size, 1), 0);
		ccv_nnc_tensor_pin_memory(input_fit->fit);
	}
	ccv_nnc_input_fit_t* const input_fit = (ccv_nnc_input_fit_t*)output_data[0];
	memset(input_fit->input->data.f32, 0, sizeof(float) * total_size * 3 * 32 * 32);
	float* mean = reduce_context->mean;
	int i;
	for (i = 0; i < total_size; i++)
	{
		const int b = i % batch_size;
		ccv_categorized_t* const categorized = (ccv_categorized_t*)input_data[b];
		float* const ip = input_fit->input->data.f32 + i * 32 * 32 * 3;
		float* const cp = categorized->matrix->data.f32;
		int fi, fj, fk;
		const int flip = dsfmt_genrand_close_open(&reduce_context->dsfmt) >= 0.5;
		const int padx = (int)(dsfmt_genrand_close_open(&reduce_context->dsfmt) * 8 + 0.5) - 4;
		const int pady = (int)(dsfmt_genrand_close_open(&reduce_context->dsfmt) * 8 + 0.5) - 4;
		if (!flip)
		{
			for (fi = ccv_max(0, pady); fi < ccv_min(32 + pady, 32); fi++)
				for (fj = ccv_max(0, padx); fj < ccv_min(32 + padx, 32); fj++)
					for (fk = 0; fk < 3; fk++)
						ip[fi * 32 + fj + fk * 32 * 32] = cp[(fi - pady) * 32 * 3 + (fj - padx) * 3 + fk] - mean[fk];
		} else {
			for (fi = ccv_max(0, pady); fi < ccv_min(32 + pady, 32); fi++)
				for (fj = ccv_max(0, padx); fj < ccv_min(32 + padx, 32); fj++)
					for (fk = 0; fk < 3; fk++)
						ip[fi * 32 + (31 - fj) + fk * 32 * 32] = cp[(fi - pady) * 32 * 3 + (fj - padx) * 3 + fk] - mean[fk];
		}
		assert(categorized->c >= 0 && categorized->c < 10);
		input_fit->fit->data.f32[i] = categorized->c;
	}
}

typedef struct {
	int device_id;
	int batch_size;
} ccv_nnc_map_context_t;

static void _copy_to_gpu(void*** const column_data, const int column_size, const int batch_size, void** const data, void* const context, ccv_nnc_stream_context_t* const stream_context)
{
	ccv_nnc_map_context_t* const map_context = (ccv_nnc_map_context_t*)context;
	ccv_nnc_tensor_param_t input = GPU_TENSOR_NCHW(000, map_context->batch_size, 3, 32, 32);
	ccv_nnc_tensor_param_t fit = GPU_TENSOR_NCHW(000, map_context->batch_size, 1);
	CCV_TENSOR_SET_DEVICE_ID(input.type, map_context->device_id);
	CCV_TENSOR_SET_DEVICE_ID(fit.type, map_context->device_id);
	int i;
	for (i = 0; i < batch_size; i++)
	{
		if (!data[i])
		{
			ccv_nnc_input_fit_t* const input_fit = (ccv_nnc_input_fit_t*)(data[i] = ccmalloc(sizeof(ccv_nnc_input_fit_t)));
			input_fit->input = ccv_nnc_tensor_new(0, input, 0);
			input_fit->fit = ccv_nnc_tensor_new(0, fit, 0);
		}
		ccv_nnc_input_fit_t* const gpu_input_fit = data[i];
		ccv_nnc_input_fit_t* const input_fit = column_data[0][i];
		ccv_nnc_tensor_t input = ccv_nnc_tensor(input_fit->input->data.f32 + map_context->device_id * map_context->batch_size * 3 * 32 * 32, CPU_TENSOR_NCHW(map_context->batch_size, 3, 32, 32), 0);
		ccv_nnc_tensor_t fit = ccv_nnc_tensor(input_fit->fit->data.f32 + map_context->device_id * map_context->batch_size, CPU_TENSOR_NCHW(map_context->batch_size, 1), 0);
		ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(&input, &fit), TENSOR_LIST(gpu_input_fit->input, gpu_input_fit->fit), stream_context);
	}
}

static void train_cifar_10(ccv_array_t* const training_set, const int batch_size, const float mean[3], ccv_array_t* const test_set)
{
	ccv_cnnp_model_t* const cifar_10 = _cifar_10_resnet56();
	const int device_count = ccv_nnc_device_count(CCV_STREAM_CONTEXT_GPU);
	ccv_nnc_tensor_param_t input = GPU_TENSOR_NCHW(000, batch_size, 3, 32, 32);
	float learn_rate = 0.001;
	ccv_cnnp_model_compile(cifar_10, &input, 1, CMD_SGD_FORWARD(learn_rate, 0.99, 0.9, 0.9), CMD_CATEGORICAL_CROSSENTROPY_FORWARD());
	FILE *w = fopen("cifar-10.dot", "w+");
	ccv_cnnp_model_dot(cifar_10, CCV_NNC_LONG_DOT_GRAPH, w);
	fclose(w);
	int i, j, k;
	ccv_nnc_tensor_t* input_tensors[device_count];
	for (i = 0; i < device_count; i++)
	{
		CCV_TENSOR_SET_DEVICE_ID(input.type, i);
		input_tensors[i] = ccv_nnc_tensor_new(0, input, 0);
	}
	ccv_nnc_tensor_t* output_tensors[2][device_count];
	for (i = 0; i < device_count; i++)
	{
		ccv_nnc_tensor_param_t output = GPU_TENSOR_NCHW(000, batch_size, 10);
		CCV_TENSOR_SET_DEVICE_ID(output.type, i);
		output_tensors[0][i] = ccv_nnc_tensor_new(0, output, 0);
		output_tensors[1][i] = ccv_nnc_tensor_new(0, output, 0);
	}
	ccv_nnc_tensor_t* fit_tensors[device_count];
	for (i = 0; i < device_count; i++)
	{
		ccv_nnc_tensor_param_t fit = GPU_TENSOR_NCHW(000, batch_size, 1);
		CCV_TENSOR_SET_DEVICE_ID(fit.type, i);
		fit_tensors[i] = ccv_nnc_tensor_new(0, fit, 0);
	}
	ccv_nnc_tensor_t* cpu_inputs[device_count];
	for (i = 0; i < device_count; i++)
	{
		cpu_inputs[i] = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(batch_size, 3, 32, 32), 0);
		ccv_nnc_tensor_pin_memory(cpu_inputs[i]);
	}
	ccv_nnc_tensor_t* cpu_fits[device_count];
	for (i = 0; i < device_count; i++)
	{
		cpu_fits[i] = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(batch_size, 1), 0);
		ccv_nnc_tensor_pin_memory(cpu_fits[i]);
	}
	ccv_nnc_tensor_t* cpu_output[device_count];
	for (i = 0; i < device_count; i++)
	{
		cpu_output[i] = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(batch_size, 10), 0);
		ccv_nnc_tensor_pin_memory(cpu_output[i]);
	}
	ccv_cnnp_column_data_t const column_data = {
		.data_enum = _array_categorized,
		.context = training_set,
	};
	ccv_cnnp_dataframe_t* const raw_data = ccv_cnnp_dataframe_new(&column_data, 1, training_set->rnum);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_reduce_context_t reduce_context = {
		.dsfmt = dsfmt,
		.mean = {
			mean[0], mean[1], mean[2]
		},
		.batch_size = batch_size,
		.device_count = device_count
	};
	ccv_cnnp_dataframe_shuffle(raw_data);
	ccv_cnnp_dataframe_t* const batch_data = ccv_cnnp_dataframe_reduce_new(raw_data, _reduce_train_batch_new, _input_fit_deinit, 0, batch_size * device_count, &reduce_context);
	int device_columns[device_count];
	ccv_nnc_map_context_t map_context[device_count];
	for (i = 0; i < device_count; i++)
	{
		map_context[i].device_id = i;
		map_context[i].batch_size = batch_size;
		int stream_type = CCV_STREAM_CONTEXT_GPU;
		CCV_STREAM_SET_DEVICE_ID(stream_type, i);
		device_columns[i] = ccv_cnnp_dataframe_map(batch_data, _copy_to_gpu, stream_type, _input_fit_deinit, COLUMN_ID_LIST(0), map_context + i);
	}
	ccv_cnnp_dataframe_iter_t* const iter = ccv_cnnp_dataframe_iter_new(batch_data, device_columns, device_count);
	int c[batch_size][device_count];
	ccv_nnc_stream_context_t* stream_contexts[2];
	stream_contexts[0] = ccv_nnc_stream_context_new(CCV_STREAM_CONTEXT_GPU);
	stream_contexts[1] = ccv_nnc_stream_context_new(CCV_STREAM_CONTEXT_GPU);
	int p = 0, q = 1;
	const int epoch_end = (training_set->rnum + batch_size * device_count - 1) / (batch_size * device_count);
	ccv_cnnp_model_set_data_parallel(cifar_10, device_count);
	ccv_cnnp_model_checkpoint(cifar_10, "cifar-10.checkpoint", 0);
	unsigned int current_time = get_current_time();
	ccv_cnnp_dataframe_iter_prefetch(iter, 1, stream_contexts[p]);
	ccv_nnc_input_fit_t* input_fits[device_count];
	ccv_nnc_tensor_t* input_fit_inputs[device_count];
	ccv_nnc_tensor_t* input_fit_fits[device_count];
	for (i = 0; i < 100000 / device_count; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter, (void**)input_fits, device_count, stream_contexts[p]);
		ccv_nnc_stream_context_wait(stream_contexts[q]); // Need to wait the other context to finish, we use the same tensor_arena.
		for (j = 0; j < device_count; j++)
		{
			input_fit_inputs[j] = input_fits[j]->input;
			input_fit_fits[j] = input_fits[j]->fit;
		}
		ccv_cnnp_model_fit(cifar_10, input_fit_inputs, device_count, input_fit_fits, device_count, output_tensors[p], device_count, stream_contexts[p]);
		// Prefetch the next round.
		ccv_cnnp_dataframe_iter_prefetch(iter, 1, stream_contexts[q]);
		if ((i + 1) % epoch_end == 0)
		{
			ccv_nnc_stream_context_wait(stream_contexts[p]);
			ccv_nnc_stream_context_wait(stream_contexts[q]);
			unsigned int elapsed_time = get_current_time() - current_time;
			ccv_cnnp_model_checkpoint(cifar_10, "cifar-10.checkpoint", 0);
			int correct = 0;
			p = 0, q = 1;
			for (j = 0; j < test_set->rnum; j += batch_size * device_count)
			{
				for (k = 0; k < ccv_min(test_set->rnum - j, batch_size * device_count); k++)
				{
					const int d = k / batch_size;
					const int b = k % batch_size;
					ccv_categorized_t* const categorized = (ccv_categorized_t*)ccv_array_get(test_set, j + k);
					float* const ip = cpu_inputs[d]->data.f32 + b * 32 * 32 * 3;
					float* const cp = categorized->matrix->data.f32;
					int fi, fj, fk;
					for (fi = 0; fi < 32; fi++)
						for (fj = 0; fj < 32; fj++)
							for (fk = 0; fk < 3; fk++)
								ip[fi * 32 + fj + fk * 32 * 32] = cp[fi * 32 * 3 + fj * 3 + fk] - mean[fk];
					assert(categorized->c >= 0 && categorized->c < 10);
					c[b][d] = categorized->c;
				}
				ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, cpu_inputs, device_count, input_tensors, device_count, 0);
				ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, cpu_fits, device_count, fit_tensors, device_count, 0);
				ccv_cnnp_model_evaluate(cifar_10, input_tensors, device_count, output_tensors[0], device_count, 0);
				ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, output_tensors[0], device_count, cpu_output, device_count, 0);
				for (k = 0; k < ccv_min(test_set->rnum - j, batch_size * device_count); k++)
				{
					const int d = k / batch_size;
					const int b = k % batch_size;
					float max = -FLT_MAX;
					int t = -1;
					int fi;
					for (fi = 0; fi < 10; fi++)
						if (cpu_output[d]->data.f32[b * 10 + fi] > max)
							max = cpu_output[d]->data.f32[b * 10 + fi], t = fi;
					if (c[b][d] == t)
						++correct;
				}
			}
			PRINT(CCV_CLI_INFO, "Epoch %03d (%d), %.2f%% (%.3f seconds)\n", (i + 1) / epoch_end, epoch_end * batch_size * device_count, (float)correct / test_set->rnum * 100, (float)elapsed_time / 1000);
			current_time = get_current_time();
			// Reshuffle and reset cursor.
			ccv_cnnp_dataframe_shuffle(raw_data);
			ccv_cnnp_dataframe_iter_set_cursor(iter, 0);
		}
		if ((i + 1) % (10000 / device_count) == 0)
		{
			learn_rate *= 0.5;
			ccv_cnnp_model_set_minimizer(cifar_10, CMD_SGD_FORWARD(learn_rate, 0.99, 0.9, 0.9));
		}
		int t;
		CCV_SWAP(p, q, t);
	}
	ccv_cnnp_dataframe_iter_free(iter);
	ccv_cnnp_dataframe_free(batch_data);
	ccv_cnnp_dataframe_free(raw_data);
	ccv_cnnp_model_free(cifar_10);
	ccv_nnc_stream_context_free(stream_contexts[0]);
	ccv_nnc_stream_context_free(stream_contexts[1]);
	for (i = 0; i < device_count; i++)
	{
		ccv_nnc_tensor_free(input_tensors[i]);
		ccv_nnc_tensor_free(fit_tensors[i]);
		ccv_nnc_tensor_free(output_tensors[0][i]);
		ccv_nnc_tensor_free(output_tensors[1][i]);
		ccv_nnc_tensor_free(cpu_inputs[i]);
		ccv_nnc_tensor_free(cpu_fits[i]);
		ccv_nnc_tensor_free(cpu_output[i]);
	}
}

int main(int argc, char** argv)
{
	ccv_nnc_init();
	assert(argc == 5);
	int num1 = atoi(argv[2]);
	int num2 = atoi(argv[4]);
	FILE* r1 = fopen(argv[1], "rb");
	FILE* r2 = fopen(argv[3], "rb");
	if (r1 && r2)
	{
		int i, j, k;
		unsigned char bytes[32 * 32 + 1];
		double mean[3] = {};
		ccv_array_t* categorizeds = ccv_array_new(sizeof(ccv_categorized_t), num1, 0);
		for (k = 0; k < num1; k++)
		{
			fread(bytes, 32 * 32 + 1, 1, r1);
			double per_mean[3] = {};
			int c = bytes[0];
			ccv_dense_matrix_t* a = ccv_dense_matrix_new(32, 32, CCV_32F | CCV_C3, 0, 0);
			for (i = 0; i < 32; i++)
				for (j = 0; j < 32; j++)
					per_mean[0] += (a->data.f32[(j + i * 32) * 3] = bytes[j + i * 32 + 1] * 2. / 255.);
			fread(bytes, 32 * 32, 1, r1);
			for (i = 0; i < 32; i++)
				for (j = 0; j < 32; j++)
					per_mean[1] += (a->data.f32[(j + i * 32) * 3 + 1] = bytes[j + i * 32] * 2. / 255.);
			fread(bytes, 32 * 32, 1, r1);
			for (i = 0; i < 32; i++)
				for (j = 0; j < 32; j++)
					per_mean[2] += (a->data.f32[(j + i * 32) * 3 + 2] = bytes[j + i * 32] * 2. / 255.);
			ccv_categorized_t categorized = ccv_categorized(c, a, 0);
			ccv_array_push(categorizeds, &categorized);
			mean[0] += per_mean[0] / (32 * 32);
			mean[1] += per_mean[1] / (32 * 32);
			mean[2] += per_mean[2] / (32 * 32);
		}
		ccv_array_t* tests = ccv_array_new(sizeof(ccv_categorized_t), num2, 0);
		for (k = 0; k < num2; k++)
		{
			fread(bytes, 32 * 32 + 1, 1, r2);
			int c = bytes[0];
			ccv_dense_matrix_t* a = ccv_dense_matrix_new(32, 32, CCV_32F | CCV_C3, 0, 0);
			for (i = 0; i < 32; i++)
				for (j = 0; j < 32; j++)
					a->data.f32[(j + i * 32) * 3] = bytes[j + i * 32 + 1] * 2. / 255.;
			fread(bytes, 32 * 32, 1, r2);
			for (i = 0; i < 32; i++)
				for (j = 0; j < 32; j++)
					a->data.f32[(j + i * 32) * 3 + 1] = bytes[j + i * 32] * 2. / 255.;
			fread(bytes, 32 * 32, 1, r2);
			for (i = 0; i < 32; i++)
				for (j = 0; j < 32; j++)
					a->data.f32[(j + i * 32) * 3 + 2] = bytes[j + i * 32] * 2. / 255.;
			ccv_categorized_t categorized = ccv_categorized(c, a, 0);
			ccv_array_push(tests, &categorized);
		}
		float meanf[3];
		meanf[0] = mean[0] / num1;
		meanf[1] = mean[1] / num1;
		meanf[2] = mean[2] / num1;
		train_cifar_10(categorizeds, 256, meanf, tests);
	}
	if (r1)
		fclose(r1);
	if (r2)
		fclose(r2);
	return 0;
}
