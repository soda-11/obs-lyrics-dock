// pinyin-engine.cpp — offline CJK → plain ASCII pinyin
// No tone diacritics. Hash-map lookup; duplicates handled naturally.
// Religious proper nouns receive initial caps (神→Shen, 耶→Ye, etc.)
#include "pinyin-engine.hpp"
#include <QString>
#include <QStringList>
#include <QChar>
#include <unordered_map>

namespace {

struct PE { uint32_t cp; const char *py; };

// Raw table — order doesn't matter; duplicates resolved (last wins) at init.
// ~850 common characters + full worship vocabulary.
static const PE kRaw[] = {
    // ── Very high frequency ──────────────────────────────────────
    {0x7684,"de"},   // 的
    {0x4E00,"yi"},   // 一
    {0x662F,"shi"},  // 是
    {0x5728,"zai"},  // 在
    {0x4E0D,"bu"},   // 不
    {0x4E86,"le"},   // 了
    {0x6709,"you"},  // 有
    {0x548C,"he"},   // 和 ← cap
    {0x4EBA,"ren"},  // 人
    {0x8FD9,"zhe"},  // 这
    {0x4E2D,"zhong"},// 中
    {0x5927,"da"},   // 大
    {0x4E3A,"wei"},  // 为
    {0x4E0A,"shang"},// 上
    {0x4E2A,"ge"},   // 个
    {0x56FD,"guo"},  // 国
    {0x6211,"wo"},   // 我
    {0x4EE5,"yi"},   // 以
    {0x8981,"yao"},  // 要
    {0x4ED6,"ta"},   // 他
    {0x65F6,"shi"},  // 时
    {0x6765,"lai"},  // 来
    {0x7528,"yong"}, // 用
    {0x4EEC,"men"},  // 们
    {0x751F,"sheng"},// 生
    {0x5230,"dao"},  // 到
    {0x4F5C,"zuo"},  // 作
    {0x5730,"di"},   // 地
    {0x4E8E,"yu"},   // 于
    {0x51FA,"chu"},  // 出
    {0x5C31,"jiu"},  // 就
    {0x5206,"fen"},  // 分
    {0x5BF9,"dui"},  // 对
    {0x6210,"cheng"},// 成
    {0x4F1A,"hui"},  // 会
    {0x53EF,"ke"},   // 可
    {0x4E3B,"zhu"},  // 主
    {0x53D1,"fa"},   // 发
    {0x5E74,"nian"}, // 年
    {0x52A8,"dong"}, // 动
    {0x540C,"tong"}, // 同
    {0x5DE5,"gong"}, // 工
    {0x4E5F,"ye"},   // 也
    {0x80FD,"neng"}, // 能
    {0x4E0B,"xia"},  // 下
    {0x8FC7,"guo"},  // 过
    {0x5B50,"zi"},   // 子
    {0x8BF4,"shuo"}, // 说
    {0x4EA7,"chan"}, // 产
    {0x79CD,"zhong"},// 种
    {0x9762,"mian"}, // 面
    {0x800C,"er"},   // 而
    {0x65B9,"fang"}, // 方
    {0x540E,"hou"},  // 后
    {0x591A,"duo"},  // 多
    {0x5B9A,"ding"}, // 定
    {0x884C,"xing"}, // 行
    {0x5B66,"xue"},  // 学
    {0x6CD5,"fa"},   // 法
    {0x6240,"suo"},  // 所
    {0x6C11,"min"},  // 民
    {0x5F97,"de"},   // 得
    {0x7ECF,"jing"}, // 经
    {0x5341,"shi"},  // 十
    {0x4E09,"san"},  // 三
    {0x4E4B,"zhi"},  // 之
    {0x8FDB,"jin"},  // 进
    {0x7740,"zhe"},  // 着
    {0x7B49,"deng"}, // 等
    {0x90E8,"bu"},   // 部
    {0x5EA6,"du"},   // 度
    {0x5BB6,"jia"},  // 家
    {0x7535,"dian"}, // 电
    {0x529B,"li"},   // 力
    {0x91CC,"li"},   // 里
    {0x5982,"ru"},   // 如
    {0x6C34,"shui"}, // 水
    {0x5316,"hua"},  // 化
    {0x9AD8,"gao"},  // 高
    {0x81EA,"zi"},   // 自
    {0x4E8C,"er"},   // 二
    {0x7406,"li"},   // 理
    {0x8D77,"qi"},   // 起
    {0x5C0F,"xiao"}, // 小
    {0x7269,"wu"},   // 物
    {0x73B0,"xian"}, // 现
    {0x5B9E,"shi"},  // 实
    {0x52A0,"jia"},  // 加
    {0x91CF,"liang"},// 量
    {0x90FD,"dou"},  // 都
    {0x4E24,"liang"},// 两
    {0x4F53,"ti"},   // 体
    {0x5236,"zhi"},  // 制
    {0x673A,"ji"},   // 机
    {0x5F53,"dang"}, // 当
    {0x4F7F,"shi"},  // 使
    {0x70B9,"dian"}, // 点
    {0x4ECE,"cong"}, // 从
    {0x4E1A,"ye"},   // 业
    {0x672C,"ben"},  // 本
    {0x53BB,"qu"},   // 去
    {0x628A,"ba"},   // 把
    {0x6027,"xing"}, // 性
    {0x597D,"hao"},  // 好
    {0x5E94,"ying"}, // 应
    {0x5F00,"kai"},  // 开
    {0x5B83,"ta"},   // 它
    {0x5408,"he"},   // 合
    {0x8FD8,"huan"}, // 还
    {0x56E0,"yin"},  // 因
    {0x7531,"you"},  // 由
    {0x5176,"qi"},   // 其
    {0x4E9B,"xie"},  // 些
    {0x7136,"ran"},  // 然
    {0x524D,"qian"}, // 前
    {0x5916,"wai"},  // 外
    {0x5929,"tian"}, // 天
    {0x653F,"zheng"},// 政
    {0x56DB,"si"},   // 四
    {0x65E5,"ri"},   // 日
    {0x90A3,"na"},   // 那
    {0x793E,"she"},  // 社
    {0x4E49,"yi"},   // 义
    {0x4E8B,"shi"},  // 事
    {0x5F62,"xing"}, // 形
    {0x76F8,"xiang"},// 相
    {0x5168,"quan"}, // 全
    {0x8868,"biao"}, // 表
    {0x95F4,"jian"}, // 间
    {0x6837,"yang"}, // 样
    {0x4E0E,"yu"},   // 与
    {0x5173,"guan"}, // 关
    {0x5404,"ge"},   // 各
    {0x91CD,"zhong"},// 重
    {0x65B0,"xin"},  // 新
    {0x7EBF,"xian"}, // 线
    {0x5185,"nei"},  // 内
    {0x6570,"shu"},  // 数
    {0x6B63,"zheng"},// 正
    {0x5FC3,"xin"},  // 心
    {0x53CD,"fan"},  // 反
    {0x4F60,"ni"},   // 你
    {0x660E,"ming"}, // 明
    {0x770B,"kan"},  // 看
    {0x5148,"xian"}, // 先
    {0x5C55,"zhan"}, // 展
    {0x8FD0,"yun"},  // 运
    {0x4E94,"wu"},   // 五
    {0x8BBE,"she"},  // 设
    {0x6D3B,"huo"},  // 活
    {0x4FE1,"xin"},  // 信
    {0x89C9,"jue"},  // 觉
    {0x6781,"ji"},   // 极
    {0x8FBE,"da"},   // 达
    {0x901A,"tong"}, // 通
    {0x975E,"fei"},  // 非
    {0x6253,"da"},   // 打
    {0x7BA1,"guan"}, // 管
    {0x539F,"yuan"}, // 原
    {0x671F,"qi"},   // 期
    {0x51B3,"jue"},  // 决
    {0x57FA,"ji"},   // 基
    {0x95EE,"wen"},  // 问
    {0x4EFB,"ren"},  // 任
    {0x6B21,"ci"},   // 次
    {0x7F8E,"mei"},  // 美
    {0x6309,"an"},   // 按
    {0x98CE,"feng"}, // 风
    {0x56DE,"hui"},  // 回
    {0x9996,"shou"}, // 首
    {0x6587,"wen"},  // 文
    {0x5B89,"an"},   // 安
    {0x53EA,"zhi"},  // 只
    {0x4E14,"qie"},  // 且
    {0x522B,"bie"},  // 别
    {0x9645,"ji"},   // 际
    {0x7279,"te"},   // 特
    {0x5199,"xie"},  // 写
    {0x8BA1,"ji"},   // 计
    {0x5C06,"jiang"},// 将
    {0x8DDF,"gen"},  // 跟
    {0x5C11,"shao"}, // 少
    {0x66F4,"geng"}, // 更
    {0x50CF,"xiang"},// 像
    {0x7ED3,"jie"},  // 结
    {0x96C6,"ji"},   // 集
    {0x754C,"jie"},  // 界
    {0x683C,"ge"},   // 格
    {0x89C4,"gui"},  // 规
    {0x6708,"yue"},  // 月
    {0x6B65,"bu"},   // 步
    {0x6D77,"hai"},  // 海
    {0x5EFA,"jian"}, // 建
    {0x60F3,"xiang"},// 想
    {0x8BB0,"ji"},   // 记
    {0x7ACB,"li"},   // 立
    {0x8BDD,"hua"},  // 话
    {0x957F,"chang"},// 长
    {0x5E26,"dai"},  // 带
    {0x7CFB,"xi"},   // 系
    {0x6C14,"qi"},   // 气
    {0x95E8,"men"},  // 门
    {0x53F7,"hao"},  // 号
    {0x611F,"gan"},  // 感
    {0x547D,"ming"}, // 命
    {0x544A,"gao"},  // 告
    {0x77E5,"zhi"},  // 知
    {0x53C8,"you"},  // 又
    {0x540D,"ming"}, // 名
    {0x53D7,"shou"}, // 受
    {0x8BED,"yu"},   // 语
    // ── Numbers and basics ───────────────────────────────────────
    {0x4E01,"ding"}, // 丁
    {0x4E03,"qi"},   // 七
    {0x4E07,"wan"},  // 万
    {0x4E08,"zhang"},// 丈
    {0x4E16,"shi"},  // 世
    {0x4E1C,"dong"}, // 东
    {0x4E1D,"si"},   // 丝
    {0x4E25,"yan"},  // 严
    {0x4E30,"feng"}, // 丰
    {0x4E3A,"wei"},  // 为
    {0x4E45,"jiu"},  // 久
    {0x4E48,"me"},   // 么
    {0x4E4E,"hu"},   // 乎
    {0x4E50,"le"},   // 乐
    {0x4E59,"yi"},   // 乙
    {0x4E5D,"jiu"},  // 九
    {0x4E60,"xi"},   // 习
    {0x4E66,"shu"},  // 书
    {0x4E70,"mai"},  // 买
    {0x4E91,"yun"},  // 云
    {0x4E92,"hu"},   // 互
    {0x4E9A,"ya"},   // 亚
    {0x4EA1,"wang"}, // 亡
    {0x4EA4,"jiao"}, // 交
    {0x4EA6,"yi"},   // 亦
    {0x4EAE,"liang"},// 亮
    {0x4EB2,"qin"},  // 亲
    {0x4EC1,"ren"},  // 仁
    {0x4EC5,"jin"},  // 仅
    {0x4ECD,"reng"}, // 仍
    {0x4ED4,"zi"},   // 仔
    {0x4ED8,"fu"},   // 付
    {0x4EE3,"dai"},  // 代
    {0x4EE4,"ling"}, // 令
    {0x4EEA,"yi"},   // 仪
    {0x4EF0,"yang"}, // 仰
    {0x4EFD,"fen"},  // 份
    {0x4EFF,"fang"}, // 仿
    {0x4F0A,"yi"},   // 伊
    {0x4F20,"chuan"},// 传
    {0x4F24,"shang"},// 伤
    {0x4F2F,"bo"},   // 伯
    {0x4F38,"shen"}, // 伸
    {0x4F46,"dan"},  // 但
    {0x4F4D,"wei"},  // 位
    {0x4F4E,"di"},   // 低
    {0x4F4F,"zhu"},  // 住
    {0x4F55,"he"},   // 何
    {0x4F5B,"fo"},   // 佛
    {0x4F73,"jia"},  // 佳
    {0x4F9B,"gong"}, // 供
    {0x4F9D,"yi"},   // 依
    {0x4FDD,"bao"},  // 保
    {0x4FEE,"xiu"},  // 修
    {0x501A,"yi"},   // 倚
    {0x503C,"zhi"},  // 值
    {0x5065,"jian"}, // 健
    {0x5144,"xiong"},// 兄
    {0x5149,"guang"},// 光
    {0x514B,"ke"},   // 克
    {0x514D,"mian"}, // 免
    {0x5165,"ru"},   // 入
    {0x516B,"ba"},   // 八
    {0x516C,"gong"}, // 公
    {0x516D,"liu"},  // 六
    {0x5171,"gong"}, // 共
    {0x5174,"xing"}, // 兴
    {0x5175,"bing"}, // 兵
    {0x5178,"dian"}, // 典
    {0x518D,"zai"},  // 再
    {0x51AC,"dong"}, // 冬
    {0x51B0,"bing"}, // 冰
    {0x51B7,"leng"}, // 冷
    {0x51C0,"jing"}, // 净
    {0x5200,"dao"},  // 刀
    {0x5207,"qie"},  // 切
    {0x520A,"kan"},  // 刊
    {0x521B,"chuang"},// 创
    {0x521D,"chu"},  // 初
    {0x5229,"li"},   // 利
    {0x522A,"shan"}, // 删 variant
    {0x529F,"gong"}, // 功
    {0x52A1,"wu"},   // 务
    {0x52A9,"zhu"},  // 助
    {0x52AA,"nu"},   // 努
    {0x52C7,"yong"}, // 勇
    {0x5317,"bei"},  // 北
    {0x533A,"qu"},   // 区
    {0x5343,"qian"}, // 千
    {0x5347,"sheng"},// 升
    {0x5348,"wu"},   // 午
    {0x534A,"ban"},  // 半
    {0x534E,"hua"},  // 华 ← cap
    {0x5357,"nan"},  // 南
    {0x535A,"bo"},   // 博
    {0x5360,"zhan"}, // 占
    {0x53C2,"can"},  // 参
    {0x53E3,"kou"},  // 口
    {0x53E4,"gu"},   // 古
    {0x53E5,"ju"},   // 句
    {0x53F0,"tai"},  // 台
    {0x53F3,"you"},  // 右
    {0x5409,"ji"},   // 吉
    {0x542C,"ting"}, // 听
    {0x547C,"hu"},   // 呼
    {0x5531,"chang"},// 唱
    {0x5584,"shan"}, // 善
    {0x559C,"xi"},   // 喜
    {0x5609,"jia"},  // 嘉
    {0x5706,"yuan"}, // 圆
    {0x5723,"sheng"},// 圣 ← cap
    {0x573A,"chang"},// 场
    {0x575A,"jian"}, // 坚
    {0x57CE,"cheng"},// 城
    {0x58EB,"shi"},  // 士
    {0x58F0,"sheng"},// 声
    {0x5904,"chu"},  // 处
    {0x5927,"da"},   // 大
    {0x592A,"tai"},  // 太
    {0x592B,"fu"},   // 夫
    {0x5931,"shi"},  // 失
    {0x5947,"qi"},   // 奇
    {0x5973,"nv"},   // 女
    {0x5976,"nai"},  // 奶
    {0x5979,"ta"},   // 她
    {0x5B54,"kong"}, // 孔
    {0x5B57,"zi"},   // 字
    {0x5B63,"ji"},   // 季
    {0x5B8C,"wan"},  // 完
    {0x5B97,"zong"}, // 宗
    {0x5B9D,"bao"},  // 宝
    {0x5BA3,"xuan"}, // 宣
    {0x5BAB,"gong"}, // 宫
    {0x5BC6,"mi"},   // 密
    {0x5BCC,"fu"},   // 富
    {0x5BD2,"han"},  // 寒
    {0x5BF8,"cun"},  // 寸
    {0x5C38,"shi"},  // 尸
    {0x5C4A,"jie"},  // 届
    {0x5C71,"shan"}, // 山
    {0x5C81,"sui"},  // 岁
    {0x5CF0,"feng"}, // 峰
    {0x5D07,"chong"},// 崇
    {0x5DE6,"zuo"},  // 左
    {0x5DE8,"ju"},   // 巨
    {0x5DF2,"yi"},   // 已
    {0x5E01,"bi"},   // 币
    {0x5E03,"bu"},   // 布
    {0x5E72,"gan"},  // 干
    {0x5E73,"ping"}, // 平
    {0x5E78,"xing"}, // 幸
    {0x5E7B,"huan"}, // 幻
    {0x5E7F,"guang"},// 广
    {0x5E86,"qing"}, // 庆
    {0x5E8F,"xu"},   // 序
    {0x5E95,"di"},   // 底
    {0x5EB7,"kang"}, // 康
    {0x5EF6,"yan"},  // 延
    {0x5F13,"gong"}, // 弓
    {0x5F3A,"qiang"},// 强
    {0x5F52,"gui"},  // 归
    {0x5F55,"lu"},   // 录
    {0x5F7B,"che"},  // 彻
    {0x5F80,"wang"}, // 往
    {0x5F85,"dai"},  // 待
    {0x5F8B,"lv"},   // 律
    {0x5FAE,"wei"},  // 微
    {0x5FC5,"bi"},   // 必
    {0x5FC6,"yi"},   // 忆
    {0x5FCD,"ren"},  // 忍
    {0x5FD7,"zhi"},  // 志
    {0x5FD8,"wang"}, // 忘
    {0x5FE0,"zhong"},// 忠
    {0x5FEB,"kuai"}, // 快
    {0x5FE7,"you"},  // 忧
    {0x5FF5,"nian"}, // 念
    {0x601C,"lian"}, // 怜
    {0x601D,"si"},   // 思
    {0x6025,"ji"},   // 急
    {0x6028,"yuan"}, // 怨
    {0x6046,"gong"}, // 恭
    {0x6050,"kong"}, // 恐
    {0x6052,"heng"}, // 恒
    {0x6069,"en"},   // 恩
    {0x60A0,"you"},  // 悠
    {0x60C5,"qing"}, // 情
    {0x60CA,"jing"}, // 惊
    {0x60E0,"hui"},  // 惠
    {0x6108,"yu"},   // 愈
    {0x610F,"yi"},   // 意
    {0x6124,"fen"},  // 愤
    {0x613F,"yuan"}, // 愿
    {0x618B,"chou"}, // 愁
    {0x6148,"ci"},   // 慈
    {0x6162,"man"},  // 慢
    {0x6170,"wei"},  // 慰
    {0x6176,"qing"}, // 庆
    {0x6212,"jie"},  // 戒
    {0x6218,"zhan"}, // 战
    {0x6267,"zhi"},  // 执
    {0x627E,"zhao"}, // 找
    {0x627F,"cheng"},// 承
    {0x628A,"ba"},   // 把
    {0x62A4,"hu"},   // 护
    {0x62A5,"bao"},  // 报
    {0x62C5,"dan"},  // 担
    {0x62C9,"la"},   // 拉
    {0x62EF,"zheng"},// 拯
    {0x6301,"chi"},  // 持
    {0x6307,"zhi"},  // 指
    {0x6316,"wa"},   // 挖
    {0x6325,"hui"},  // 挥
    {0x632A,"gua"},  // 挂
    {0x6389,"diao"}, // 掉
    {0x638C,"zhang"},// 掌
    {0x63A5,"jie"},  // 接
    {0x63A8,"tui"},  // 推
    {0x63D0,"ti"},   // 提
    {0x6536,"shou"}, // 收
    {0x653E,"fang"}, // 放
    {0x6545,"gu"},   // 故
    {0x6548,"xiao"}, // 效
    {0x654F,"min"},  // 敏
    {0x6574,"zheng"},// 整
    {0x6587,"wen"},  // 文
    {0x6599,"liao"}, // 料
    {0x65AD,"duan"}, // 断
    {0x65C5,"lv"},   // 旅
    {0x65E7,"jiu"},  // 旧
    {0x660C,"chang"},// 昌
    {0x6613,"yi"},   // 易
    {0x661F,"xing"}, // 星
    {0x6625,"chun"}, // 春
    {0x663E,"xian"}, // 显
    {0x6668,"chen"}, // 晨
    {0x666E,"pu"},   // 普
    {0x666F,"jing"}, // 景
    {0x6697,"an"},   // 暗
    {0x670B,"peng"}, // 朋
    {0x670D,"fu"},   // 服
    {0x671B,"wang"}, // 望
    {0x671D,"chao"}, // 朝
    {0x6728,"mu"},   // 木
    {0x672A,"wei"},  // 未
    {0x6743,"quan"}, // 权
    {0x6761,"tiao"}, // 条
    {0x676F,"bei"},  // 杯
    {0x6797,"lin"},  // 林
    {0x6807,"biao"}, // 标
    {0x6811,"shu"},  // 树
    {0x6839,"gen"},  // 根
    {0x683C,"ge"},   // 格
    {0x68C0,"jian"}, // 检
    {0x68EE,"sen"},  // 森
    {0x690D,"zhi"},  // 植
    {0x6982,"gai"},  // 概
    {0x6B22,"huan"}, // 欢
    {0x6B27,"ou"},   // 欧
    {0x6B32,"yu"},   // 欲
    {0x6B64,"ci"},   // 此
    {0x6B66,"wu"},   // 武
    {0x6B7B,"si"},   // 死
    {0x6BCF,"mei"},  // 每
    {0x6BCD,"mu"},   // 母
    {0x6BD4,"bi"},   // 比
    {0x6C11,"min"},  // 民
    {0x6C38,"yong"}, // 永
    {0x6C49,"han"},  // 汉
    {0x6C57,"han"},  // 汗
    {0x6C5F,"jiang"},// 江
    {0x6C89,"chen"}, // 沉
    {0x6CA1,"mo"},   // 没
    {0x6CB3,"he"},   // 河
    {0x6CB9,"you"},  // 油
    {0x6CC9,"quan"}, // 泉
    {0x6CE5,"ni"},   // 泥
    {0x6CF0,"tai"},  // 泰
    {0x6D0B,"yang"}, // 洋
    {0x6D17,"xi"},   // 洗
    {0x6D1E,"dong"}, // 洞
    {0x6D41,"liu"},  // 流
    {0x6D74,"yu"},   // 浴
    {0x6D85,"nie"},  // 涅
    {0x6DB5,"han"},  // 涵
    {0x6DF1,"shen"}, // 深
    {0x6DFB,"tian"}, // 添
    {0x6E05,"qing"}, // 清
    {0x6E29,"wen"},  // 温
    {0x6E34,"ke"},   // 渴
    {0x6E38,"you"},  // 游
    {0x6EE1,"man"},  // 满
    {0x6F14,"yan"},  // 演
    {0x6FC0,"ji"},   // 激
    {0x706B,"huo"},  // 火
    {0x706D,"mie"},  // 灭
    {0x706F,"deng"}, // 灯
    {0x7075,"ling"}, // 灵
    {0x707D,"zai"},  // 灾
    {0x7089,"lu"},   // 炉
    {0x70B9,"dian"}, // 点
    {0x70ED,"re"},   // 热
    {0x71C3,"ran"},  // 燃
    {0x722C,"pa"},   // 爬
    {0x7231,"ai"},   // 爱
    {0x7236,"fu"},   // 父
    {0x7237,"ye"},   // 爷
    {0x7238,"ba"},   // 爸
    {0x7247,"pian"}, // 片
    {0x724C,"pai"},  // 牌
    {0x725B,"niu"},  // 牛
    {0x7259,"ya"},   // 牙
    {0x726F,"mu"},   // 牯 variant
    {0x72C2,"kuang"},// 狂
    {0x72EC,"du"},   // 独
    {0x7384,"xuan"}, // 玄
    {0x73A9,"wan"},  // 玩
    {0x73ED,"ban"},  // 班
    {0x7389,"yu"},   // 玉
    {0x751A,"shen"}, // 甚
    {0x751C,"tian"}, // 甜
    {0x7537,"nan"},  // 男
    {0x753B,"hua"},  // 画
    {0x756A,"fan"},  // 番
    {0x7591,"yi"},   // 疑
    {0x75AF,"feng"}, // 疯
    {0x767D,"bai"},  // 白
    {0x767E,"bai"},  // 百
    {0x76AE,"pi"},   // 皮
    {0x76D7,"dao"},  // 盗
    {0x76D8,"pan"},  // 盘
    {0x76F4,"zhi"},  // 直
    {0x76EE,"mu"},   // 目
    {0x773C,"yan"},  // 眼
    {0x77E5,"zhi"},  // 知
    {0x77F3,"shi"},  // 石
    {0x7834,"po"},   // 破
    {0x793A,"shi"},  // 示
    {0x793C,"li"},   // 礼
    {0x793E,"she"},  // 社
    {0x7956,"zu"},   // 祖
    {0x795D,"zhu"},  // 祝
    {0x795E,"shen"}, // 神 ← cap
    {0x796D,"ji"},   // 祭
    {0x7965,"xiang"},// 祥
    {0x798F,"fu"},   // 福
    {0x7981,"jin"},  // 禁
    {0x79CD,"zhong"},// 种
    {0x7A0B,"cheng"},// 程
    {0x7A33,"wen"},  // 稳
    {0x7A4D,"ji"},   // 积
    {0x7A63,"su"},   // 稣 ← cap
    {0x7A7A,"kong"}, // 空
    {0x7AE0,"zhang"},// 章
    {0x7ACB,"li"},   // 立
    {0x7B26,"fu"},   // 符
    {0x7B2C,"di"},   // 第
    {0x7B54,"da"},   // 答
    {0x7B56,"ce"},   // 策
    {0x7B97,"suan"}, // 算
    {0x7BC7,"pian"}, // 篇
    {0x7C7B,"lei"},  // 类
    {0x7CBE,"jing"}, // 精
    {0x7EBF,"xian"}, // 线
    {0x7ECF,"jing"}, // 经
    {0x7ED3,"jie"},  // 结
    {0x7ED9,"gei"},  // 给
    {0x7EDF,"tong"}, // 统
    {0x7EF3,"sheng"},// 绳
    {0x7F57,"luo"},  // 罗
    {0x7F6A,"zui"},  // 罪
    {0x7FA4,"qun"},  // 群
    {0x7FFB,"fan"},  // 翻
    {0x8001,"lao"},  // 老
    {0x8003,"kao"},  // 考
    {0x8010,"nai"},  // 耐
    {0x8033,"er"},   // 耳
    {0x8036,"ye"},   // 耶 ← cap
    {0x804C,"zhi"},  // 职
    {0x805A,"ju"},   // 聚
    {0x80A1,"gu"},   // 股
    {0x80FD,"neng"}, // 能
    {0x8138,"lian"}, // 脸
    {0x81EA,"zi"},   // 自
    {0x820C,"she"},  // 舌
    {0x8212,"shu"},  // 舒
    {0x821E,"wu"},   // 舞
    {0x8239,"chuan"},// 船
    {0x826F,"liang"},// 良
    {0x8272,"se"},   // 色
    {0x82B1,"hua"},  // 花
    {0x82CD,"cang"}, // 苍
    {0x82E5,"ruo"},  // 若
    {0x827A,"yi"},   // 艺
    {0x8302,"mao"},  // 茂
    {0x8336,"cha"},  // 茶
    {0x8349,"cao"},  // 草
    {0x8352,"huang"},// 荒
    {0x8363,"rong"}, // 荣
    {0x843D,"luo"},  // 落
    {0x84DD,"lan"},  // 蓝
    {0x8499,"meng"}, // 蒙
    {0x85CF,"cang"}, // 藏
    {0x864E,"hu"},   // 虎
    {0x865A,"xu"},   // 虚
    {0x866B,"chong"},// 虫
    {0x8840,"xue"},  // 血
    {0x884C,"xing"}, // 行
    {0x8863,"yi"},   // 衣
    {0x88AB,"bei"},  // 被
    {0x8981,"yao"},  // 要
    {0x89BD,"lan"},  // 览
    {0x89C1,"jian"}, // 见
    {0x89D2,"jiao"}, // 角
    {0x89E3,"jie"},  // 解
    {0x8BA1,"ji"},   // 计
    {0x8BA4,"ren"},  // 认
    {0x8BAD,"xun"},  // 训
    {0x8BB0,"ji"},   // 记
    {0x8BBE,"she"},  // 设
    {0x8BC1,"zheng"},// 证
    {0x8BC4,"ping"}, // 评
    {0x8BF8,"zhu"},  // 诸
    {0x8BFA,"nuo"},  // 诺
    {0x8C03,"tiao"}, // 调
    {0x8C6A,"hao"},  // 豪
    {0x8D1F,"fu"},   // 负
    {0x8D22,"cai"},  // 财
    {0x8D4E,"shu"},  // 赎
    {0x8D5E,"zan"},  // 赞
    {0x8D70,"zou"},  // 走
    {0x8DEF,"lu"},   // 路
    {0x8DD1,"pao"},  // 跑
    {0x8DF3,"tiao"}, // 跳
    {0x8DFF,"ta"},   // 踏
    {0x8EAB,"shen"}, // 身
    {0x8F6C,"zhuan"},// 转
    {0x8F93,"shu"},  // 输
    {0x8F9B,"xin"},  // 辛
    {0x8FBE,"da"},   // 达
    {0x8FC7,"guo"},  // 过
    {0x8FDB,"jin"},  // 进
    {0x8FDE,"lian"}, // 连
    {0x8FD0,"yun"},  // 运
    {0x8FD1,"jin"},  // 近
    {0x8FD8,"huan"}, // 还
    {0x8FD9,"zhe"},  // 这
    {0x8FDC,"yuan"}, // 远
    {0x9000,"tui"},  // 退
    {0x9001,"song"}, // 送
    {0x9002,"shi"},  // 适
    {0x9003,"tao"},  // 逃
    {0x9010,"zhu"},  // 逐
    {0x901A,"tong"}, // 通
    {0x9020,"zao"},  // 造
    {0x9038,"yi"},   // 逸
    {0x903B,"luo"},  // 逻
    {0x9053,"dao"},  // 道
    {0x9047,"yu"},   // 遇
    {0x9063,"qian"}, // 遣
    {0x9065,"yao"},  // 遥
    {0x9075,"zun"},  // 遵
    {0x9152,"jiu"},  // 酒
    {0x9178,"suan"}, // 酸
    {0x9192,"xing"}, // 醒
    {0x91D1,"jin"},  // 金
    {0x9274,"jian"}, // 鉴
    {0x95E8,"men"},  // 门
    {0x9614,"kuo"},  // 阔
    {0x961F,"dui"},  // 队
    {0x9636,"jie"},  // 阶
    {0x9648,"chen"}, // 陈
    {0x9677,"xian"}, // 陷
    {0x96BE,"nan"},  // 难
    {0x96C6,"ji"},   // 集
    {0x96EA,"xue"},  // 雪
    {0x97F3,"yin"},  // 音
    {0x9875,"ye"},   // 页
    {0x9879,"xiang"},// 项
    {0x987B,"xu"},   // 须
    {0x9898,"ti"},   // 题
    {0x98CE,"feng"}, // 风
    {0x98DE,"fei"},  // 飞
    {0x9928,"guan"}, // 馆
    {0x9999,"xiang"},// 香
    {0x9A6C,"ma"},   // 马
    {0x9A91,"qi"},   // 骑
    {0x9C7C,"yu"},   // 鱼
    {0x9E1F,"niao"}, // 鸟
    {0x9ED1,"hei"},  // 黑
    {0x9F99,"long"}, // 龙
    // ── Additional worship / praise vocabulary ───────────────────
    {0x54C8,"ha"},   // 哈 (哈利路亚)
    {0x8DEF,"lu"},   // 路
    {0x4E9A,"ya"},   // 亚 (哈利路亚)
    {0x5C4B,"wu"},   // 屋 variant
    {0x591C,"ye"},   // 夜
    {0x6052,"heng"}, // 恒
    {0x771F,"zhen"}, // 真
    {0x6551,"jiu"},  // 救
    {0x7236,"fu"},   // 父
    {0x8000,"yao"},  // 耀
    {0x6885,"mei"},  // 梅 variant / 美 extra
    {0x5965,"ao"},   // 奥
    {0x5854,"ta"},   // 塔
    {0x57CB,"mai"},  // 埋
    {0x62DC,"bai"},  // 拜
    {0x656C,"jing"}, // 敬
    {0x5954,"ben"},  // 奔
    {0x538C,"yan"},  // 厌
    {0x80E1,"hu"},   // 胡
    {0x66F9,"cao"},  // 曹
    {0x5413,"xia"},  // 吓
    {0x5440,"yo"},   // 哟
    {0x554A,"a"},    // 啊
    {0x5566,"la"},   // 啦
    {0x56F0,"kun"},  // 困
    {0x7763,"du"},   // 督
    {0x57F9,"pei"},  // 培
    {0x5E02,"shi"},  // 市
    {0x95F0,"men"},  // 闷 variant
    {0x5723,"sheng"},// 圣 (dupe handled)
    // Traditional form extras
    {0x5929,"tian"}, // 天 (dupe OK)
    {0x5C71,"shan"}, // 山 (dupe OK)
};

static constexpr int kRawSize = static_cast<int>(sizeof(kRaw) / sizeof(kRaw[0]));

// Characters whose first pinyin letter gets initial cap (religious proper nouns).
static const uint32_t kCapChars[] = {
    0x534E, // 华 → Hua  (耶和华)
    0x548C, // 和 → He   (耶和华)
    0x5723, // 圣 → Sheng
    0x795E, // 神 → Shen
    0x7A63, // 稣 → Su   (耶稣)
    0x8036, // 耶 → Ye
};
static constexpr int kCapSize = static_cast<int>(sizeof(kCapChars) / sizeof(kCapChars[0]));

// Lazy-built lookup map (handles duplicate entries: last definition wins).
static const char *lookupPinyin(uint32_t cp)
{
    static std::unordered_map<uint32_t, const char *> map = [](){
        std::unordered_map<uint32_t, const char *> m;
        m.reserve(kRawSize * 2);
        for (const auto &e : kRaw) m[e.cp] = e.py;
        return m;
    }();
    auto it = map.find(cp);
    return it != map.end() ? it->second : nullptr;
}

static bool shouldCap(uint32_t cp)
{
    for (int i = 0; i < kCapSize; ++i)
        if (kCapChars[i] == cp) return true;
    return false;
}

// Chinese punctuation code-point ranges/values to silently skip.
static bool isCJKPunctuation(uint32_t cp)
{
    if (cp >= 0x3000 && cp <= 0x303F) return true; // CJK Symbols & Punctuation
    if (cp >= 0xFF00 && cp <= 0xFF0F) return true; // Fullwidth ASCII punctuation
    if (cp >= 0xFE30 && cp <= 0xFE4F) return true; // CJK Compatibility Forms
    switch (cp) {
        case 0x2014: case 0x2015: // em dash
        case 0x2018: case 0x2019: // smart single quotes
        case 0x201C: case 0x201D: // smart double quotes
        case 0x2026: // ellipsis
        case 0xFF01: case 0xFF0C: case 0xFF1A: case 0xFF1B:
        case 0xFF1F: case 0xFF08: case 0xFF09:
            return true;
        default: return false;
    }
}

} // namespace

namespace PinyinEngine {

QString generate(const QString &chineseText)
{
    QStringList outLines;
    for (const QString &line : chineseText.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            outLines << QString();
            continue;
        }
        QStringList tokens;
        for (QChar ch : line) {
            if (ch.isSpace()) continue;
            uint32_t cp = static_cast<uint32_t>(ch.unicode());
            if (isCJKPunctuation(cp)) continue;

            const char *py = lookupPinyin(cp);
            if (py && *py) {
                QString s = QString::fromLatin1(py);
                if (shouldCap(cp) && !s.isEmpty())
                    s[0] = s[0].toUpper();
                tokens << s;
            } else if (cp < 0x2E80) {
                // ASCII / Latin: pass through (numbers, English words, etc.)
                tokens << QString(ch);
            }
            // Unknown CJK: silently skip
        }
        outLines << tokens.join(QLatin1Char(' '));
    }
    return outLines.join(QLatin1Char('\n'));
}

} // namespace PinyinEngine
