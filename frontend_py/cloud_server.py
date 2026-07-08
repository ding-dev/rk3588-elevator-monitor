from flask import Flask, request, jsonify
from flask_cors import CORS
import os

app = Flask(__name__)
CORS(app) # 允许网页跨域上传

# 在当前目录创建一个 cloud_uploads 文件夹存视频
UPLOAD_FOLDER = os.path.join(os.getcwd(), 'cloud_uploads')
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

@app.route('/upload', methods=['POST'])
def upload_file():
    if 'video' not in request.files:
        return jsonify({"error": "No file uploaded"}), 400
        
    file = request.files['video']
    if file.filename == '':
        return jsonify({"error": "Empty filename"}), 400
        
    # 保存文件
    save_path = os.path.join(UPLOAD_FOLDER, file.filename)
    file.save(save_path)
    
    # 获取本机 IP 
    # 组合成真实的下载链接返回给网页
    download_url = f"http://192.168.100.10:6034/download/{file.filename}"
    print(f"File uploaded successfully: {download_url}")
    
    return jsonify({"url": download_url})

# 提供文件下载服务
@app.route('/download/<filename>')
def download_file(filename):
    from flask import send_from_directory
    return send_from_directory(UPLOAD_FOLDER, filename)

if __name__ == '__main__':
    print("🚀 Cloud File Server is running on port 5000...")
    app.run(host='0.0.0.0', port=6034)
