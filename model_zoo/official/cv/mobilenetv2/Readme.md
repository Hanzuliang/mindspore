# Contents

- [MobileNetV2 Description](#mobilenetv2-description)
- [Model Architecture](#model-architecture)
- [Dataset](#dataset)
- [Features](#features)
    - [Mixed Precision](#mixed-precision)
- [Environment Requirements](#environment-requirements)
- [Script Description](#script-description)
    - [Script and Sample Code](#script-and-sample-code)
    - [Training Process](#training-process)
    - [Evaluation Process](#evaluation-process)
        - [Evaluation](#evaluation)
- [Model Description](#model-description)
    - [Performance](#performance)  
        - [Training Performance](#evaluation-performance)
        - [Inference Performance](#evaluation-performance)
- [Description of Random Situation](#description-of-random-situation)
- [ModelZoo Homepage](#modelzoo-homepage)

# [MobileNetV2 Description](#contents)


MobileNetV2 is tuned to mobile phone CPUs through a combination of hardware- aware network architecture search (NAS) complemented by the NetAdapt algorithm and then subsequently improved through novel architecture advances.Nov 20, 2019.

[Paper](https://arxiv.org/pdf/1905.02244) Howard, Andrew, Mark Sandler, Grace Chu, Liang-Chieh Chen, Bo Chen, Mingxing Tan, Weijun Wang et al. "Searching for MobileNetV2." In Proceedings of the IEEE International Conference on Computer Vision, pp. 1314-1324. 2019.

# [Model architecture](#contents)

The overall network architecture of MobileNetV2 is show below:

[Link](https://arxiv.org/pdf/1905.02244)

# [Dataset](#contents)

Dataset used: [imagenet](http://www.image-net.org/)

- Dataset size: ~125G, 1.2W colorful images in 1000 classes
	- Train: 120G, 1.2W images
	- Test: 5G, 50000 images
- Data format: RGB images.
	- Note: Data will be processed in src/dataset.py 


# [Features](#contents)

## [Mixed Precision(Ascend)](#contents)

The [mixed precision](https://www.mindspore.cn/tutorial/zh-CN/master/advanced_use/mixed_precision.html) training method accelerates the deep learning neural network training process by using both the single-precision and half-precision data formats, and maintains the network precision achieved by the single-precision training at the same time. Mixed precision training can accelerate the computation process, reduce memory usage, and enable a larger model or batch size to be trained on specific hardware. 
For FP16 operators, if the input data type is FP32, the backend of MindSpore will automatically handle it with reduced precision. Users could check the reduced-precision operators by enabling INFO log and then searching ‘reduce precision’.

# [Environment Requirements](#contents)

- Hardware（Ascend/GPU）
  - Prepare hardware environment with Ascend or GPU processor. If you want to try Ascend  , please send the [application form](https://obs-9be7.obs.cn-east-2.myhuaweicloud.com/file/other/Ascend%20Model%20Zoo%E4%BD%93%E9%AA%8C%E8%B5%84%E6%BA%90%E7%94%B3%E8%AF%B7%E8%A1%A8.docx) to ascend@huawei.com. Once approved, you can get the resources. 
- Framework
  - [MindSpore](http://10.90.67.50/mindspore/archive/20200506/OpenSource/me_vm_x86/)
- For more information, please check the resources below：
  - [MindSpore tutorials](https://www.mindspore.cn/tutorial/zh-CN/master/index.html) 
  - [MindSpore API](https://www.mindspore.cn/api/zh-CN/master/index.html)


# [Script description](#contents)

## [Script and sample code](#contents)

```python
├── MobileNetV2        
  ├── Readme.md     # descriptions about MobileNetV2                 
  ├── scripts 
  │   ├──run_train.sh   # shell script for train               
  │   ├──run_eval.sh    # shell script for evaluation                
  ├── src                              
  │   ├──config.py      # parameter configuration               
  │   ├──dataset.py     # creating dataset
  │   ├──launch.py      # start python script
  │   ├──lr_generator.py     # learning rate config                            
  │   ├──mobilenetV2.py      # MobileNetV2 architecture
  ├── train.py      # training script
  ├── eval.py       #  evaluation script
```

## [Training process](#contents)

### Usage


You can start training using python or shell scripts. The usage of shell scripts as follows:

- Ascend: sh run_train.sh Ascend [DEVICE_NUM] [VISIABLE_DEVICES(0,1,2,3,4,5,6,7)] [RANK_TABLE_FILE] [DATASET_PATH] [CKPT_PATH]
- GPU: sh run_trian.sh GPU [DEVICE_NUM] [VISIABLE_DEVICES(0,1,2,3,4,5,6,7)] [DATASET_PATH]

### Launch

``` 
# training example
  python:
      Ascend: python train.py --dataset_path ~/imagenet/train/ --device_targe Ascend
      GPU: python train.py --dataset_path ~/imagenet/train/ --device_targe GPU

  shell:
      Ascend: sh run_train.sh Ascend 8 0,1,2,3,4,5,6,7 hccl_config.json ~/imagenet/train/ mobilenet_199.ckpt
      GPU: sh run_train.sh GPU 8 0,1,2,3,4,5,6,7 ~/imagenet/train/
```

### Result

Training result will be stored in the example path. Checkpoints will be stored at `. /checkpoint` by default, and training log  will be redirected to `./train/train.log` like followings. 

``` 
epoch: [  0/200], step:[  624/  625], loss:[5.258/5.258], time:[140412.236], lr:[0.100]
epoch time: 140522.500, per step time: 224.836, avg loss: 5.258
epoch: [  1/200], step:[  624/  625], loss:[3.917/3.917], time:[138221.250], lr:[0.200]
epoch time: 138331.250, per step time: 221.330, avg loss: 3.917
```

## [Eval process](#contents)

### Usage

You can start training using python or shell scripts. The usage of shell scripts as follows:

- Ascend: sh run_infer.sh Ascend [DATASET_PATH] [CHECKPOINT_PATH]
- GPU: sh run_infer.sh GPU [DATASET_PATH] [CHECKPOINT_PATH]

### Launch

``` 
# infer example
  python:
      Ascend: python eval.py --dataset_path ~/imagenet/val/ --checkpoint_path mobilenet_199.ckpt --device_targe Ascend
      GPU: python eval.py --dataset_path ~/imagenet/val/ --checkpoint_path mobilenet_199.ckpt --device_targe GPU

  shell:
      Ascend: sh run_infer.sh Ascend ~/imagenet/val/ ~/train/mobilenet-200_625.ckpt
      GPU: sh run_infer.sh GPU ~/imagenet/val/ ~/train/mobilenet-200_625.ckpt
```

> checkpoint can be produced in training process. 

### Result

Inference result will be stored in the example path, you can find result like the followings in `val.log`. 

``` 
result: {'acc': 0.71976314102564111} ckpt=/path/to/checkpoint/mobilenet-200_625.ckpt
```

# [Model description](#contents)

## [Performance](#contents)

### Training Performance

| Parameters                 | MobilenetV2                                                |                           |
| -------------------------- | ---------------------------------------------------------- | ------------------------- |
| Model Version              |                                                            | large                     |
| Resource                   | Ascend 910, cpu:2.60GHz 56cores, memory:314G               | NV SMX2 V100-32G          |
| uploaded Date              | 05/06/2020                                                 | 05/06/2020                |
| MindSpore Version          | 0.3.0                                                      | 0.3.0                     |
| Dataset                    | ImageNet                                                   | ImageNet                  |
| Training Parameters        | src/config.py                                              | src/config.py             |
| Optimizer                  | Momentum                                                   | Momentum                  |
| Loss Function              | SoftmaxCrossEntropy                                        | SoftmaxCrossEntropy       |
| outputs                    |                                                            |                           |
| Loss                       |                                                            | 1.913                     |
| Accuracy                   |                                                            | ACC1[77.09%] ACC5[92.57%] |
| Total time                 |                                                            |                           |
| Params (M)                 |                                                            |                           |
| Checkpoint for Fine tuning |                                                            |                           |
| Model for inference        |                                                            |                           |

#### Inference Performance

| Parameters                 |                               |                           |                      |
| -------------------------- | ----------------------------- | ------------------------- | -------------------- |
| Model Version              | V1                            |                           |                      |
| Resource                   | Huawei 910                    | NV SMX2 V100-32G          | Huawei 310           |
| uploaded Date              | 05/06/2020                    | 05/22/2020                |                      |
| MindSpore Version          | 0.2.0                         | 0.2.0                     | 0.2.0                | 
| Dataset                    | ImageNet, 1.2W                | ImageNet, 1.2W            | ImageNet, 1.2W       |
| batch_size                 |                               | 130(8P)                   |                      |
| outputs                    |                               |                           |                      |
| Accuracy                   |                               | ACC1[72.07%] ACC5[90.90%] |                      |
| Speed                      |                               |                           |                      |
| Total time                 |                               |                           |                      |
| Model for inference        |                               |                           |                      |

# [Description of Random Situation](#contents)

In dataset.py, we set the seed inside “create_dataset" function. We also use random seed in train.py.

# [ModelZoo Homepage](#contents)
 
Please check the official [homepage](https://gitee.com/mindspore/mindspore/tree/master/model_zoo). 
 