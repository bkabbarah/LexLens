from flask import Flask, request, jsonify, send_from_directory, render_template_string
from PIL import Image, ImageOps, ImageFilter
import io
import pytesseract
from googletrans import Translator
import logging
import os
import datetime
import cv2
import numpy as np

app = Flask(__name__)
translator = Translator()

logging.basicConfig(level=logging.DEBUG)

# make image directory 
IMAGES_DIR = 'captured_images'
os.makedirs(IMAGES_DIR, exist_ok=True)

def preprocess_image(image):
    cv_image = np.array(image)
    
    # grayscale
    if len(cv_image.shape) == 3:
        cv_image = cv2.cvtColor(cv_image, cv2.COLOR_RGB2GRAY)
        app.logger.debug("Converted image to grayscale using OpenCV.")
    
    # gaussian blur for noise reduction
    cv_image = cv2.GaussianBlur(cv_image, (3, 3), 0)
    app.logger.debug("Applied Gaussian blur for noise reduction using OpenCV.")
    
    #  CLAHE for contrast enhancement
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    cv_image = clahe.apply(cv_image)
    app.logger.debug("Applied CLAHE for contrast enhancement using OpenCV.")
    
    # adaptive thresholding
    cv_image = cv2.adaptiveThreshold(cv_image, 255,
                                    cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
                                    cv2.THRESH_BINARY, 11, 2)
    app.logger.debug("Applied adaptive thresholding using OpenCV.")
    
    # morphological operations
    kernel = np.ones((2,2), np.uint8)
    cv_image = cv2.morphologyEx(cv_image, cv2.MORPH_CLOSE, kernel)
    app.logger.debug("Applied morphological operations using OpenCV.")
    
    #back to PIL Image
    processed_image = Image.fromarray(cv_image)
    
    return processed_image

@app.route('/upload', methods=['POST'])
def upload_image():
    try:
        # read raw binary data 
        image_bytes = request.data
        if not image_bytes:
            app.logger.error("No image data received.")
            return jsonify({'error': 'No image data received'}), 400

        # load image using PIL
        image = Image.open(io.BytesIO(image_bytes))
        app.logger.debug("Image received and loaded successfully.")

        # orient
        image = image.rotate(-90, expand=True)  # Negative for clockwise rotation
        image = image.transpose(method=Image.FLIP_LEFT_RIGHT)
        app.logger.debug("Rotated image by 90 degrees clockwise.")

        image = preprocess_image(image)

        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        image_filename = f"image_{timestamp}.jpg"
        image_path = os.path.join(IMAGES_DIR, image_filename)
        image.save(image_path)
        app.logger.info(f"Saved image to {image_path}")
        # OCR
        text = pytesseract.image_to_string(image, lang='ara')  # arabic
        app.logger.info(f"OCR Output: {text}")

        # translate text
        if text.strip():
            translation = translator.translate(text, src='ar', dest='en')  # Spanish to English
            translated_text = translation.text
            app.logger.info(f"Translated Text: {translated_text}")
        else:
            translated_text = "No text detected."
            app.logger.info("No text detected in the image.")

        return jsonify({'translated_text': translated_text})

    except Exception as e:
        app.logger.exception("Error processing the image.")
        return jsonify({'error': str(e)}), 500

@app.route('/images', methods=['GET'])
def list_images():
    # HTML page to show all captured images (debugging purposes)
    images = sorted(os.listdir(IMAGES_DIR), reverse=True)
    image_tags = ''
    for img in images[:10]:  # Show the latest 10 images
        image_url = f"/images/{img}"
        image_tags += f'<div><img src="{image_url}" alt="{img}" style="max-width:300px;"><p>{img}</p></div><hr>'
    html = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>Captured Images</title>
    </head>
    <body>
        <h1>Latest Captured Images</h1>
        {image_tags if image_tags else '<p>No images captured yet.</p>'}
    </body>
    </html>
    """
    return render_template_string(html)

@app.route('/images/<filename>', methods=['GET'])
def get_image(filename):
    return send_from_directory(IMAGES_DIR, filename)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
