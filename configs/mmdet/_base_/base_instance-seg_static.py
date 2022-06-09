_base_ = ['./base_static.py']

onnx_config = dict(output_names=['dets'])
codebase_config = dict(post_processing=dict(export_postprocess_mask=False))
