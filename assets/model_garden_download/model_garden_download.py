# Copyright (c) 2025 Ambarella International LP
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import yaml
import os
import shutil
from huggingface_hub import hf_hub_download


def download_model_garden(args):
    model_garden_info_yaml = args.model_garden_info_yaml
    output_dir = args.output_dir
    os.makedirs(output_dir, exist_ok=True)

    with open(model_garden_info_yaml, "r", encoding='utf-8') as f:
        model_garden_info = yaml.safe_load(f)

    assert 'models' in model_garden_info, f"models must be in the file {args.model_garden_info_yaml}"
    download_info = {}
    download_info['base_dir'] = os.path.abspath(output_dir)
    download_info['models'] = {}
    for model_info in model_garden_info['models']:
        model_name = model_info['repo_id'].split('/')[-1]
        local_dir=os.path.join(output_dir, model_name)
        model_download_info = {
            'repo_id': model_info['repo_id'],
            'revision': None if args.latest_revision or 'revision' not in model_info else model_info['revision'],
            'files': {}
        }

        for file_name, file_path in model_info['files'].items():
            print(f"Downloading {file_path} to {local_dir}")
            model_path = hf_hub_download(
                repo_id=model_info['repo_id'],
                revision=None if args.latest_revision or 'revision' not in model_info else model_info['revision'],
                local_dir=local_dir,
                filename = file_path,
                force_download=args.force_download,
            )
            assert os.path.exists(model_path), f"File {model_path} does not exist"

            if file_path.endswith('.tar'):
                tar_file_path = os.path.join(local_dir, file_path)
                tar_dir_path = tar_file_path[:-4]
                os.makedirs(tar_dir_path, exist_ok=True)
                print(f"Extracting {tar_file_path} to {tar_dir_path}")
                os.system(f"tar -xf {tar_file_path} -C {tar_dir_path}")
                os.remove(tar_file_path)
                model_download_info['files'][file_name] = os.path.join(model_name, file_path[:-4])
            else:
                model_download_info['files'][file_name] = os.path.join(model_name, file_path)

        download_info['models'][model_name] = model_download_info
        if args.remove_cache:
            print(f"Removing cache directory {os.path.join(local_dir, '.cache')}")
            shutil.rmtree(os.path.join(local_dir, '.cache'))

    with open(os.path.join(output_dir, 'model_garden_download_info.yaml'), 'w', encoding='utf-8') as f:
        yaml.dump(download_info, f, allow_unicode=True, sort_keys=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Model Garden Download")
    parser.add_argument("model_garden_info_yaml", help="model garden information YAML file")
    parser.add_argument("output_dir", help="output directory")
    parser.add_argument("--latest_revision", action="store_true", help="download latest revision")
    parser.add_argument("--force_download", action="store_true", help="whether the file should be downloaded even if it already exists in the local cache.")
    parser.add_argument("--remove_cache", action="store_true", help="whether to remove the cache directory.")

    args = parser.parse_args()
    download_model_garden(args)
