dtbo-$(CONFIG_ARCH_KALAMA) := kalama-camera.dtbo

#dtbo-$(CONFIG_ARCH_KALAMA) += kalama-camera-sensor-cdp.dtbo \
#								kalama-camera-sensor-mtp.dtbo \
#								kalama-camera-sensor-qrd.dtbo \
#								kalama-camera-sensor-hdk.dtbo \
#								kalama-sg-hhg-camera.dtbo \
#								kalama-sg-hhg-camera-sensor.dtbo

#PLUS_DTS_OVERLAY start
dtbo-$(CONFIG_ARCH_KALAMA) += oplus/salami-camera-overlay.dtbo \

dtbo-$(CONFIG_ARCH_KALAMA) += oplus/xueying-camera-overlay.dtbo \

dtbo-$(CONFIG_ARCH_KALAMA) += oplus/xigua-camera-overlay.dtbo  \

dtbo-$(CONFIG_ARCH_KALAMA) += oplus/aston-camera-overlay.dtbo  \

dtbo-$(CONFIG_ARCH_KALAMA) += oplus/aston-camera-overlay-T0.dtbo  \

dtbo-$(CONFIG_ARCH_KALAMA) += oplus/astondomastic-camera-overlay-T0.dtbo  \

#OPLUS_DTS_OVERLAY end

# dtbo-$(CONFIG_ARCH_CROW) += crow-camera.dtbo \
# 				crow-camera-sensor-idp.dtbo \
# 				crow-camera-sensor-qrd.dtbo

dtbo-$(CONFIG_ARCH_CROW) += crow-camera.dtbo \
				oplus/benz-camera-overlay.dtbo

