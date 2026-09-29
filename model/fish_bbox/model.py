from __future__ import annotations

from typing import Any

import tensorflow as tf

from .dataset import INPUT_SIZE

DEFAULT_LEARNING_RATE = 1e-4


@tf.keras.utils.register_keras_serializable(package="FishBBox")
def bbox_loss(y_true: Any, y_pred: Any) -> tf.Tensor:
    coordinates_loss = tf.reduce_mean(tf.square(y_true[:, :4] - y_pred[:, :4]), axis=-1)
    confidence_loss = tf.square(y_true[:, 4] - y_pred[:, 4])
    return coordinates_loss + 2.0 * confidence_loss


def build_model(
    input_size: int = INPUT_SIZE,
    learning_rate: float = DEFAULT_LEARNING_RATE,
) -> tf.keras.Model:
    inputs = tf.keras.Input(shape=(input_size, input_size, 1), name="image")
    features = tf.keras.layers.Conv2D(16, 3, strides=2, padding="same", activation="relu")(inputs)
    features = tf.keras.layers.DepthwiseConv2D(3, strides=2, padding="same", activation="relu")(features)
    features = tf.keras.layers.Conv2D(32, 3, strides=2, padding="same", activation="relu")(features)
    features = tf.keras.layers.DepthwiseConv2D(3, strides=2, padding="same", activation="relu")(features)
    features = tf.keras.layers.Conv2D(64, 3, strides=1, padding="same", activation="relu")(features)
    features = tf.keras.layers.Flatten()(features)
    outputs = tf.keras.layers.Dense(5, activation="linear", name="fish_bbox")(features)
    model = tf.keras.Model(inputs=inputs, outputs=outputs, name="fish_bbox")
    model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=learning_rate), loss=bbox_loss)
    return model


def build_mobilenetv2_model(
    input_size: int = INPUT_SIZE,
    learning_rate: float = DEFAULT_LEARNING_RATE,
) -> tuple[tf.keras.Model, tf.keras.Model]:
    inputs = tf.keras.Input(shape=(input_size, input_size, 1), name="image")
    rgb = tf.keras.layers.Concatenate(name="grayscale_to_rgb")([inputs, inputs, inputs])
    normalized = tf.keras.layers.Rescaling(
        scale=2.0, offset=-1.0, name="mobilenet_input_scaling"
    )(rgb)
    backbone = tf.keras.applications.MobileNetV2(
        input_shape=(input_size, input_size, 3),
        alpha=0.35,
        include_top=False,
        weights="imagenet",
        name="mobilenetv2_backbone",
    )
    backbone.trainable = False
    features = backbone(normalized, training=False)
    features = tf.keras.layers.GlobalAveragePooling2D(name="global_average_pooling")(features)
    outputs = tf.keras.layers.Dense(5, activation="sigmoid", name="fish_bbox")(features)
    model = tf.keras.Model(inputs=inputs, outputs=outputs, name="fish_bbox_mobilenetv2")
    model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=learning_rate), loss=bbox_loss)
    return model, backbone