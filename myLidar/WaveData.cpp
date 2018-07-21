#include "WaveData.h"

#define PulseWidth 4		//定义激光脉冲宽度做剥离阈值参考
#define TimeDifference 8	//与UTC的时差


/*功能：  高斯核生成
//kernel：存储生成的高斯核
//size：  核的大小
//sigma： 正态分布标准差
*/
void gau_kernel(float kernel[], int size, float sigma)
{
	if (size <= 0 || sigma == 0)
		return;
	int x;
	float sum = 0;
	int m = (size - 1) / 2;

	//get kernel	
	for (x = 0; x <size; x++)
	{
		kernel[x] = (1 / sigma * sqrt(2 * 3.1415)) * exp(-(x - m)*(x - m) / 2 * sigma*sigma);
		sum += kernel[x];
	}

	//normal
	for (x = 0; x < size; x++)
	{
		kernel[x] /= sum;
	}
}


/*功能:	 高斯模糊
//src：  输入原图
//dst：  模糊图像
//size： 核的大小
//sigma：正态分布标准差
*/
void gaussian(float src[], float dst[])
{
	float kernel[5];
	gau_kernel(kernel, 5, 1);
	//gaussian卷积,此时边界没加处理
	for (int i = (5 - 1) / 2; i <= 319 - (5 - 1) / 2; i++)
	{
		dst[i] = src[i - 2] * kernel[0] + src[i - 1] * kernel[1] + src[i] * kernel[2] + src[i + 1] * kernel[3] + src[i + 2] * kernel[4];
	}
}


/*功能：	假设函数模型
//*p:	代求参数
//*x：  原始数据（测量值）
//m：	参数维度
//n：	测量值维度
//*data:？
*/
void expfun(double *p, double *x, int m, int n, void *data)
{
	register int i;
	for (i = 0; i<n; ++i)
	{
		//写出参数与x[i]之间的关系式，由于这里方程的右边没有观测值，所以只有参数
		x[i] = p[0] * exp(-(i - p[1])*(i - p[1]) / (2 * p[2])*(2 * p[2])) + p[3] * exp(-(i - p[4])*(i - p[4]) / (2 * p[5])*(2 * p[5]));
	}

}


/*功能：	函数模型的雅可比矩阵
//*p:	代求参数
//jac： 雅可比矩阵参数
//m：	参数维度
//n：	测量值维度
//*data:？
*/
void jacexpfun(double *p, double *jac, int m, int n, void *data)
{
	register int i, j;
	//写出雅克比矩阵
	for (i = j = 0; i<n; ++i)
	{
		jac[j++] = exp(-(i - p[1])*(i - p[1]) / (2 * p[2])*p[2]);
		jac[j++] = p[0] * (i - p[1]) / (p[2] * p[2])*exp(-(i - p[1])*(i - p[1]) / (2 * p[2] * p[2]));
		jac[j++] = p[0] * (i - p[1])*(i - p[1]) / (p[2] * p[2] * p[2])*exp(-(i - p[1])*(i - p[1]) / (2 * p[2] * p[2]));

		jac[j++] = exp(-(i - p[4])*(i - p[4]) / (2 * p[5])*p[5]);
		jac[j++] = p[3] * (i - p[4]) / (p[5] * p[5])*exp(-(i - p[4])*(i - p[4]) / (2 * p[5] * p[5]));
		jac[j++] = p[3] * (i - p[4])*(i - p[4]) / (p[5] * p[5] * p[5])*exp(-(i - p[4])*(i - p[4]) / (2 * p[5] * p[5]));
	}
}


WaveData::WaveData()
{

};

WaveData::~WaveData()
{
	//手动释放vector内存，不知道有没有必要性
	vector<float>().swap(m_BlueWave);
	vector<float>().swap(m_GreenWave);
	vector<GaussParameter>().swap(m_BlueGauPra);
	vector<GaussParameter>().swap(m_GreenGauPra);
};


/*功能：	提取原始数据中的兴趣区域数据
//*&hs:	原始Lidar数据
*/
void WaveData::GetData(HS_Lidar &hs)
{
	//GPS->UTC->BeiJing
	PGPSTIME pgt = new GPSTIME;
	PCOMMONTIME pct = new COMMONTIME;
	pgt->wn = (int)hs.header.nGPSWeek;
	pgt->tow.sn = (long)hs.header.dGPSSecond;
	pgt->tow.tos = 0;
	GPSTimeToCommonTime(pgt, pct);
	m_time.year = pct->year;
	m_time.month = pct->month;
	m_time.day = pct->day;
	m_time.hour = pct->hour + TimeDifference;	//直接转化为北京时间
	m_time.minute = pct->minute;
	m_time.second = pct->second;
	delete pgt;
	delete pct;

	//取蓝绿通道
	m_BlueWave.assign(&hs.CH2.nD0[0], &hs.CH2.nD0[320]);
	m_GreenWave.assign(&hs.CH3.nD0[0], &hs.CH3.nD0[320]);

};


/*功能：		去噪滤波函数
//&srcWave:	通道原始数据
*/
void WaveData::Filter(vector<float> &srcWave)
{
	//高斯滤波去噪
	vector<float> dstWave;
	dstWave.assign(srcWave.begin(), srcWave.end());
	gaussian(&srcWave[0], &dstWave[0]);
	srcWave.assign(dstWave.begin(), dstWave.end());
	dstWave.clear();
};


/*功能：			高斯分量分解函数
//&srcWave:		通道原始数据
//&waveParam：	该通道的高斯分量参数
*/
void WaveData::Resolve(vector<float> &srcWave, vector<GaussParameter> &waveParam)
{
	//拷贝原始数据
	float data[320],temp[320];
	int i = 0, m = 0;
	for (vector<float>::iterator iter = srcWave.begin(); iter != srcWave.end(); ++iter, ++i)
	{
		data[i] = *iter;
	}
	
	//计算噪声最小值为环境噪声
	float min = data[0];
	for (m = 0; m < 320; m++)
	{
		temp[m] = data[m];
		if (data[m] < min)	
			min = data[m];
	}

	//所有数据除去环境噪声
	for (m = 0; m < 320; m++)
	{
		temp[m] -= min;
	}
	srcWave.assign(&temp[0],&temp[320]);

	float A;	//振幅
	float b;	//脉冲距离
	float tg;	//峰值时间位置
	float tgl;	//半峰时间位置（左)
	float tgr;	//半峰时间位置（右）

	//循环剥离过程
	do
	{
		A = 0;
		//找最大值并记录位置
		for (m = 0; m < 320; m++)
		{
			if (temp[m] > A)
			{
				A = temp[m];
				b = m;
			}
		}

		//寻找半宽位置
		for (m = b; m < 319; m++)
		{
			if ((temp[m - 1] > A / 2) && (temp[m + 1] < A / 2))
			{
				tgr = m;
				break;
			}
		}
		for (m = b; m > 0; m--)
		{
			if ((temp[m - 1] < A / 2) && (temp[m + 1] > A / 2))
			{
				tgl = m;
				break;
			}
		}
		if ((b - tgl) > (tgr - b))
		{
			tg = tgr;
		}
		else
		{
			tg = tgl;
		}

		//计算sigma
		float sigma = fabs(tg - b) / sqrt(2 * log(2));

		//将该组高斯分量参数压入向量
		GaussParameter param{A,b,sigma};
		waveParam.push_back(param);

		//剥离
		for (m = 0; m < 320; m++)
		{
			if (temp[m] > A*exp(-(m - b)*(m - b) / (2 * sigma*sigma)))
			{
				temp[m] -= A*exp(-(m - b)*(m - b) / (2 * sigma*sigma));
			}
			else
				temp[m] = 0;
		}

		//判断是否继续剥离
		A = 0;
		for (m = 0; m < 320; m++)
		{
			if (temp[m] > A)
			{
				A = temp[m];
			}
		}

		//获取向量中所存结构体的第一个波峰值作阈值参考量
		gaussPraIter = waveParam.begin();
	} while (A >= 20/*Gnoise*/);//循环条件!!!值得探讨


	//对高斯分量做筛选：时间间隔小于一定值的剔除能量较小的分量，将该vector对象的sigma值设为0
	for (int i = 0; i<waveParam.size() - 1; i++)
	{
		for (int j = i + 1; j < waveParam.size(); j++)
		{
			if (abs(waveParam.at(i).b - waveParam.at(j).b) < PulseWidth)//Key
			{
				if (waveParam.at(i).A >= waveParam.at(j).A)
				{
					waveParam.at(j).sigma = 0;
				}
				else
				{
					waveParam.at(i).sigma = 0;
				}
			}
		}
	}

	//再将sigma为0值的分量剔除
	for (gaussPraIter = waveParam.begin(); gaussPraIter != waveParam.end();)
	{
		if (gaussPraIter->sigma == 0)
		{
			gaussPraIter = waveParam.erase(gaussPraIter);
		}
		else
		{
			++gaussPraIter;
		}
	}

};


/*功能：			LM算法迭代优化
//&srcWave:		通道原始数据
//&waveParam：	该通道的高斯分量参数
//LM算法参考：	https://blog.csdn.net/shajun0153/article/details/75073137
*/
void WaveData::Optimize(vector<float> &srcWave,vector<GaussParameter> &waveParam)
{
	//解算的高斯分量组数不等于两个则不优化
	if (waveParam.size()!=2)
	{
		return;
	}

	//获取高斯函数参数
	double p[6];
	int i = 0;
	for (auto gp : waveParam)
	{
		p[i++] = gp.A;
		p[i++] = gp.b;
		p[i++] = gp.sigma;
	}
	int m = i;
	int n = srcWave.size();

	//获取拟合数据
	double x[320];
	i = 0;
	for (vector<float>::iterator iter = srcWave.begin(); iter != srcWave.end(); ++iter, ++i)
	{
		x[i] = *iter;
	}
	
	double info[LM_INFO_SZ];
	// 调用迭代入口函数
	int ret = dlevmar_der(expfun,	//描述测量值之间关系的函数指针
		jacexpfun,					//估计雅克比矩阵的函数指针
		p,							//初始化的待求参数，结果一并保存在其中
		x,							//测量值
		m,							//参数维度
		n,							//测量值维度
		1000,						//最大迭代次数
		NULL,						//opts,       //迭代的一些参数
		info,						//关于最小化结果的一些参数，不需要设为NULL
		NULL, NULL, NULL			//一些内存的指针，暂时不需要
		);
	/*printf("Levenberg-Marquardt returned in %g iter, reason %g, sumsq %g [%g]\n", info[5], info[6], info[1], info[0]);
	printf("Bestfit parameters: A:%.7g b:%.7g sigma:%.7g A:%.7g b:%.7g sigma:%.7g\n", p[0], p[1], p[2], p[3], p[4], p[5]);
	printf("波峰时间差: %.7g ns\n", abs(p[4] - p[1]));*/

	//将优化后的参数组赋给vector
	i = 0;
	
	for (gaussPraIter = waveParam.begin(); gaussPraIter != waveParam.end();gaussPraIter++)
	{
		gaussPraIter->A = p[i++];
		gaussPraIter->b = p[i++];
		gaussPraIter->sigma = p[i++];
	}

};

/*功能：	自定义需要输出的信息
//内容：	年 月 日 时 分 秒 
*/
ostream &operator<<(ostream & stream, const WaveData & wavedata)
{
	stream << wavedata.m_time.year << " "
		<< wavedata.m_time.month << " "
		<< wavedata.m_time.day << " "
		<< wavedata.m_time.hour << " "
		<< wavedata.m_time.minute << " "
		<< wavedata.m_time.second;

	//兴趣数据暂定为蓝色通道的波峰位置
	if (!wavedata.m_BlueGauPra.empty())
	{
		for (auto p : wavedata.m_BlueGauPra)
		{
			stream << " "<<p.b;
		}
	}
	stream << endl;

	return stream;
}
