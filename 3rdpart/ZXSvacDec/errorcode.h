#pragma once
//解码SDK错误码定义
#define __VMERRORCODE_H_VER__ 0x01000001

#ifndef DEFINE_SVAC_ERR
#define DEFINE_SVAC_ERR

#define AUTHORIZEOK 							0
#define VIRTUALMACHINE 							(-5)		//虚拟机
#define AUTHORIZEFAIL 							(-6)		//授权失败
#define NOLICENCEFILE 							(-7)		//未找到授权
#define LICENCEFILEERROR 						(-8)		//授权文件错误
#define LICENCEFILEEXPIRED 						(-9)		//授权过期
#define PERMISSION_DENIED 						(-10)		//权限不足
#define SVAC_PLUS_ERROR 						(-11)		//SVAC_PLUS未启动
#define SVAC_LOADDLL_FAILD 						(-12)		//SVAC库加载失败
#define VSEC_LOADDLL_FAILD 						(-13)		//VSEC库加载失败
#define SVAC_UNKNOWN_ERR 						(-14)		//未知错误
#define SVAC_NO_SUPPORT_FUNCTION 				(-15)		//不支持此功能
#define LIC_FAIL_TYPE_ERR 						(-16)		//授权类型错误
#define SVAC_OUTPUT_CACHE_ERR 					(-20)		//输出缓存不足
#define INPUT_TYPE_ERR 							(-21)		//输入类型不支持
#define SVAC_HANDLE_INVALID 					(-22)		//句柄无效
#define INPUT_MEMORY_DISABLE 					(-23)		//内存地址不可用
#define PARAM_ILLEGALITY	 					(-24)		//参数不合法

#endif

#ifndef DEFINE_SVAC_DEC_STATUS
#define DEFINE_SVAC_DEC_STATUS

#define	SVAC_DEC_PIC_PASSWORD_ERROR 			0xffff0001  // 密码错，图像数据密码错
#define	SVAC_DEC_EXT_PASSWORD_ERROR 			0xffff0002  // 密码错，扩展信息密码错
#define	SVAC_DEC_PIC_EXT_PASSWORD_ERROR 		0xffff0003  // 密码错，图像数据及扩展信息均密码错
#define	SVAC_DEC_POC_ERROR 						0xffff0004  // 解码错，帧序号不连续
#define	SVAC_DEC_EXT_ERROR 						0xffff0008  // 解码错，扩展信息解码出错
#define	SVAC_DEC_POC_EXT_ERROR 					0xffff000c  // 解码错，帧序号不连续及扩展信息出错
#define	SVAC_DEC_PIC_ERROR 						0xffff0010  // 解码错，图像数据解码出错
#define	SVAC_DEC_PIC_EXT_ERROR 					0xffff0018  // 解码错，图像数据解码出错同时扩展信息解码出错
#define	SVAC_DEC_DATA_LOSS 						0xffff0020  // 解码错，图像数据不完整
#define	SVAC_DEC_NEED_MORE_BITS					0xffff0040	// 需要更多的数据
#define	SVAC_DEC_VER_ERROR						0xffff0080	// 码流version信息错误
#define SVAC_DEC_VKEKNOTMATCH					0xffff0100  // 密码错,vkek不匹配. 没有把码流对应的vkek信息配置到安全库
#define SVAC_DEC_DECVKEKERR						0xffff0200  // 密码错,vkek解密失败.对端没有使用硬件公钥加密造成vkek解密失败

#define SVAC_PREFETCH_ERR 						0xfffe0100  // 未初始化;没有SPS或可解密的SPS
#define SVAC_PREFETCH_PARAM_ERR 				0xfffe0200  // 输入参数错误
#define SVAC_PREFETCH_SEC_PS_ERR 				0xfffe0300  // 安全参数集解码错
#define SVAC_PREFETCH_SEC_PS_SETUP_ERR			0xfffe0400  // 安全库设置安全参数集函数错误
#define SVAC_PREFETCH_SEC_DECRYPT_ERR			0xfffe0500  // 安全库解密函数错误
#define SVAC_PREFETCH_SEC_DECRYPT_KEY_WRONG_ERR 0xfffe0600  // 解密时密码错
#define SVAC_PREFETCH_SEC_VKEKNOTMATCH			0xfffe0700  // 密码错,vkek不匹配. 没有把码流对应的vkek信息配置到安全库
#define SVAC_PREFETCH_SEC_DECVKEKERR			0xfffe0800  // 密码错,vkek解密失败.对端没有使用硬件公钥加密造成vkek解密失败

#define SVAC_PREFETCH_PIC_SPS_ERR				0xfffe1000  //序列参数集解码错,，此位置1
#define SVAC_PREFETCH_SEC_DECRYPT_KEY_UNCERTAIN_ERR 0xfffe2000 //解密时无法确定密码对错，此位置1
#define SVAC_PREFETCH_SEC_DECRYPT_KEY_UNDEFINE_ERR  0xfffe4000 //解密时未定义错，此位置1
#endif

#ifndef DEFINE_SVAC_VSEC_STATUS
#define DEFINE_SVAC_VSEC_STATUS

#define SVAC_PIN_INCORRECT              			0xF0000024      /* PIN 不正确 */
#define SVAC_PIN_LOCKED                 			0xF0000025      /* PIN 被锁死 */
#define SVAC_UKEY_OPEN_ERR              			0xF0000031      /* ukey 打开失败 */
#define SVAC_UKEY_UNKOWN                			0xF0000032      /* ukey 未识别 */
#define SVAC_NO_HUKEY                   			0xF0000033      /* ukey 未初始化 */
#define SVAC_UKEY_TYPEERR               			0xF0000034      /* ukey 厂家错误，pc上没有插入正确厂家的ukey */
#define SVAC_UKEY_SIGN1_ERR             			0xF0000035      /* 前端签名信息错误 */
#define SVAC_UKEY_VKEK_ERR              			0xF0000036      /* 前端vkek错误 */
//双向认证模块
#define VSEC_BDERR_BASE                 		0xF0020000
#define VSEC_BD_GETR1ERR                		(VSEC_BDERR_BASE + 0x01)
#define VSEC_BD_GETR2ERR                		(VSEC_BDERR_BASE + 0x02)
#define VSEC_BD_GETSIGN1ERR             		(VSEC_BDERR_BASE + 0x02)
#define VSEC_BD_GETSIGN2ERR             		(VSEC_BDERR_BASE + 0x03)        /* 生成sign2失败 */
#define VSEC_BD_GENCVKEKERR             		(VSEC_BDERR_BASE + 0x03)        /* 生成cvkek失败 */
#define VSEC_BD_NOCERTFOUND             		(VSEC_BDERR_BASE + 0x04)        /* 双向认证找不到对应的证书 */
#define VSEC_BD_VERIFYSIGN1ERR          		(VSEC_BDERR_BASE + 0x05)        /* 双向认证sign2验证失败 */
#define VSEC_BD_VERIFYSIGN2ERR          		(VSEC_BDERR_BASE + 0x05)        /* 双向认证sign2验证失败 */
#define VSEC_BD_VKEKDECERR              		(VSEC_BDERR_BASE + 0x09)        /* 双向认证vkek解密失败 不要改*/
#define VSEC_BD_NOVKEK2CALC             		(VSEC_BDERR_BASE + 0x07)        /* 因为没有获取到vkek无法计算SIP信令 */
#define VSEC_BD_SIPNOTMATCH             		(VSEC_BDERR_BASE + 0x08)        /* 摘要值不同,SIP信令校验失败 */
#define VSEC_BD_SIGNFORMATERR          			(VSEC_BDERR_BASE + 0x09)        /* 签名数据格式错误 */

//发证模块
#define VSEC_IMPCERTERR_BASE                	0xF0040000
#define VSEC_IMPCERTERR_KEYPAIRALGERR       	(VSEC_IMPCERTERR_BASE + 0x01)        /* 密钥对算法不支持 */
#define VSEC_IMPCERTERR_CONTENTWRONG        	(VSEC_IMPCERTERR_BASE + 0x02)        /* 导入证书格式或内容不识别 */
#define VSEC_IMPCERTERR_SIGNKEYNOTMATCH     	(VSEC_IMPCERTERR_BASE + 0x03)        /* 签名证书和私钥不匹配 */
#define VSEC_IMPCERTERR_ENCKEYNOTMATCH      	(VSEC_IMPCERTERR_BASE + 0x04)        /* 加密证书和私钥不匹配 */
#define VSEC_IMPORTCERT_DEVRETURNERR        	(VSEC_IMPCERTERR_BASE + 0x05)        /* 硬件设备返回导入失败 */
#endif

//解码棒错误码定义
#ifndef DEFINE_USB_ERR
#define DEFINE_USB_ERR

#define _EC(x)								  (0x80000000 | x)
#define VMUSB_NOERROR                            0          //no error [没有错误]
#define VMUSB_ERROR                             -1          //unknown error [未知错误]
#define VMUSB_SYSTEM_ERROR                      _EC(1)      //the current operating system cannot support AVX [当前操纵系统无法支持AVX集]
#define VMUSB_NETWORK_ERROR		                _EC(2)      //net error，maybe because net timeout [网络错误，可能是因为网络超时]
#define VMUSB_DEV_VER_NOMATCH		            _EC(3)      //device protocal not mathched [设备协议不匹配]
//初始化相关
#define VMUSB_SDK_INIT_ERROR		            _EC(11)     //error occur while initializing SDK [SDK初始化出错]
#define VMUSB_SDK_UNINIT_ERROR                  _EC(12)     //error occur while cleanuping SDK [SDK清理出错]
#define VMUSB_NO_INIT                           _EC(13)     //CLientSDK not initialize [CLientSDK未经初始化]
//打开设备及通道相关
#define VMUSB_DEV_GETDEVLIST_ERROR              _EC(21)     //failed to get the device list [获取设备列表失败]
#define VMUSB_COMMIT_ERROR                      _EC(22)     //can't comit now [暂时无法执行]
#define VMUSB_COMMIT_STATUS_ERROR               _EC(23)     //the current status is incorrect [当前状态不正确]
#define VMUSB_OPEN_CHANNEL_ERROR	            _EC(24)     //fail to open channel [打开通道失败]
#define VMUSB_CLOSE_CHANNEL_ERROR	            _EC(25)     //fail to close channel [关闭通道失败]
#define VMUSB_OPEN_SEC_CHANNEL_NUMBER_ERROR     _EC(26)     //Opening the channel num is incorrect [打开通道数不正确]
#define VMUSB_OPEN_SEC_CHANNEL_SETMODE_ERROR    _EC(27)     //Opening the channel mode is incorrect [打开通道的模式不正确]
#define VMUSB_CHANNEL_ALLOC_ERROR               _EC(28)     //usbchannel_start alloc failed [通道分配失败]
#define VMUSB_OPEN_BD_CHANNEL_ERROR             _EC(29)     //open BD channel failed [打开双向认证通道失败]
#define VMUSB_DEVICE_NEGOTAITION_FAILED         _EC(30)		//negotaition failed[硬件不支持]

//解码相关
#define VMUSB_DEC_OPEN_ERROR                    _EC(31)     //error occur while opening decode library [打开解码库出错]
#define VMUSB_DEC_CLOSE_ERROR                   _EC(32)     //error occur while closing decode library [关闭解码库出错]
#define VMUSB_WRITEPILE_ERROR                   _EC(33)     //failed to write data to pipeline [向管道内写数据失败]
#define VMUSB_DECODE_WAIT_YUV_TIMEOUT_ERROR     _EC(34)     //wait for YUV to time out [等待YUV超时]
#define VMUSB_BULK_DWTYPE_ERROR                 _EC(35)     //dwtype is error [数据头错误]
#define VMUSB_BULK_PAYLOAD_TYPE_ERROR           _EC(36)     //payload_type is error [payload类型错误]
#define VMUSB_PARSE_USB_IN_BUFFER_ERROR         _EC(37)     //parse usbin buffer failed [解析 USB 缓冲区失败]
#define VMUSB_INSUFFICIENT_BUFFER               _EC(38)     //not enough buffer [没有足够的缓存]
#define VMUSB_DEVICE_OFF                        _EC(39)     //the device drops [设备掉落]      
#define VMUSB_WAIT_CMD_ERROR                    _EC(40)     //wait cmd failed [加锁等待失败]
#define VMUSB_ALLOC_MEMORY_ERROR                _EC(41)     //alloc memory failed [分配内存失败]
//其他相关bug
#define VMUSB_OPEN_FILE_ERROR                   _EC(51)     //open file fail [打开文件出错]
#define VMUSB_ILLEGAL_PARAM		                _EC(52)     //user params not valid [用户参数不合法]
#define VMUSB_INVALID_HANDLE		            _EC(53)     //invalid handle [句柄无效]
#define VMUSB_EMPTY_LIST                        _EC(54)     //the result list of query is empty [查询结果为空]
#define VMUSB_RETURN_DATA_ERROR                 _EC(55)     //check returned value unvalid [对返回数据的校验失败]
#define VMUSB_INSUFFICIENT_BUFFER               _EC(56)     //not enough buffer [没有足够的缓存]
#define VMUSB_EMPTY_LIST                        _EC(57)     //the result list of query is empty [查询结果为空]

#endif

