# ねね将棋 第28回世界コンピュータ将棋選手権(WCSC28)版
2018年5月に出場したバージョンです。

Deep Learningを用いた評価関数で探索を行う将棋プログラム。1次予選にて、40チーム中15位。

環境構築がとても面倒なので、使用はおすすめしません。アイデアの参考としてご利用ください。このリポジトリはやねうら王を改造したもののため、GPLv3ライセンスとします。

# 構成
- このリポジトリのmasterブランチ
  - 本体となるUSIエンジン(`YaneuraOu-user.exe`)とpythonモジュール(`pyshogieval.pyd`)。
- このリポジトリの[python-dllブランチ](https://github.com/select766/neneshogi_wcsc28_YaneuraOu/tree/python-dll)
  - 学習時に使うpythonモジュール(`yaneuraou.pyd`)。
- neneshogiリポジトリ [https://github.com/select766/neneshogi](https://github.com/select766/neneshogi/tree/2018-wcsc28-final)
  - deep learning関係。対局用コードと学習用コードの両方が入っている。
- ipqueueリポジトリ [https://github.com/select766/ipqueue](https://github.com/select766/ipqueue/tree/d63ca8d87e5966014bcfa5edd560f7cef193853e)
  - プロセス間通信用ライブラリ。git submoduleとしてロードされる。

# 環境
Visual Studio 2017, Python 3.6, CUDA 9.0, cuDNN 7.1

# masterブランチのセットアップ、ビルド
```
git submodule init
git submodule update  # ipqueueリポジトリをロード
```

Visual Studio 2017にて、"Release-user"構成でビルド。

YaneuraOuプロジェクトから、`build/user/YaneuraOu-user.exe`が、pyshogievalプロジェクトから、`pyshogieval/pyshogieval/pyshogieval.pyd`が生成されるはず。

```
cd pyshogieval
python setup.py develop  # python環境にpyshogievalモジュールをインストール
```

# python-dllブランチのセットアップ、ビルド
masterとは別の場所にcloneすることを推奨。学習時のみ必要で、エンジンの実行には不要。

Visual Studio 2017にて、"Release-user"構成でビルド。

`python/yaneuraou/yaneuraou.pyd`が生成されるはず。

```
cd python
python setup.py develop
```

# neneshogiリポジトリのセットアップ
リポジトリをclone

```
pip install -r requirements.txt
python setup.py develop
```

`neneshogi_cpp` Visual Studio プロジェクトは不使用のはず。

# 将棋所からの利用
USIエンジンとして`build/user/YaneuraOu-user.exe`を登録。

設定(デフォルトでないものを **太字** で表示)
- **Threads** : 2
- Hash: 16
- MultiPV: 1
- WriteDebugLog: false
- NetworkDelay: 120
- NetworkDelay2: 1120
- MinimumThinkingTime: 2000
- **SlowMover** : 150
- MaxMovesToDraw: 0
- DepthLimit: 0
- **NodesLimit** : 1000000
- Contempt: 2
- ContemptFromBlack: false
- **EnteringKingRule** : CSARule27
- EvalDir: eval
- PvInterval: 1000
- **MCTSHash** : 80
- **c_puct** : 2.84
- play_temperature: 1.0
- **softmax** : 0.86
- value_scale: 1.0
- value_slope: 1.0
- virtual_loss: 1
- clear_table: false
- **model** : (モデルのウェイトファイルへの絶対パス)
- **initial_tt** : (定跡置換表ファイルのあるパス)
- **batch_size** : 512
- **process_per_gpu** : 2
- **gpu_max** : 7
- **gpu_min** : 0
- **block_queue_length** : 20

本番モデルは`data\model\20180316103614\weight\model_iter_1953125`
本番置換表は`data\make_book\first\ttable\merged.bin`

# 学習例
やねうら王形式の40byte/recordの棋譜データを用意する。

http://yaneuraou.yaneu.com/2018/01/23/%E6%9C%88%E5%88%8A%E6%95%99%E5%B8%AB%E5%B1%80%E9%9D%A2-2018%E5%B9%B41%E6%9C%88%E5%8F%B7/

`data/model/20180316103614`に学習の設定が書いてあるとき、
```
python -m neneshogi.train data/model/20180316103614
```
データセットのファイル名を環境に合わせて書き換えたり、`solver.yaml`の`initmodel`行を初期値となるモデルがないなら削除したりする必要がある。


以下、オリジナルのやねうら王のREADME
====

# About this project

YaneuraOu mini is a shogi engine(AI player), stronger than Bonanza6 , educational and tiny code(about 2500 lines) , USI compliant engine , capable of being compiled by VC++2017

やねうら王miniは、将棋の思考エンジンで、Bonanza6より強く、教育的で短いコード(2500行程度)で書かれたUSIプロトコル準拠の思考エンジンで、VC++2017でコンパイル可能です。

[やねうら王mini 公式サイト (解説記事、開発者向け情報等)](http://yaneuraou.yaneu.com/YaneuraOu_Mini/)

[やねうら王公式 ](http://yaneuraou.yaneu.com/)

# お知らせ

- SDT5(第5回 将棋電王トーナメント)のため、10月1日～11月12日はプルリクエストが処理できません。
- SDT5直前は大改造をすることがあるので、動作等が安定していない可能性が高いです。(現状、V4.76が安定)

# 現在進行中のサブプロジェクト

## やねうら王2017 Early

2017年5月5日完成。この思考エンジンを用いたelmoがWCSC27で優勝しました。elmo(WCSC27)や蒼天幻想ナイツ・オブ・タヌキ(WCSC27出場)の評価関数を用いるとXeon 24コアでR4000程度の模様。

- 思考エンジン本体のダウンロードは[こちら](https://github.com/yaneurao/YaneuraOu/releases/)

## やねうら王2017 GOKU : SDT5に『極やねうら王』として出場。

2017年11月の第5回将棋電王トーナメントに参加してきました。(探索部の強さ、前バージョンからあまり変わらず)　Apery(SDT5)などの評価関数を用いるとXeon 24コアでR4200程度の模様。


## やねうら王2018 OTAFUKU (やねうら王2018 with お多福ラボ)

今年は一年、これでいきます。


## やねうら王詰め将棋solver

《tanuki-さんが開発中》

長手数の詰将棋が解けるsolverです。


# 過去のサブプロジェクト

過去のサブプロジェクトである、やねうら王nano , mini , classic、王手将棋、取る一手将棋、協力詰めsolver、連続自己対戦フレームワークなどはこちらからどうぞ。

- [過去のサブプロジェクト](/docs/README2017.md)

## やねうら王評価関数ファイル

- やねうら王2017Early用 - Apery(WCSC26)、Apery(SDT4)＝「浮かむ瀬」の評価関数バイナリがそのまま使えます。
- やねうら王2017 KPP_KKPT型評価関数 - 以下のKPP_KKPT型ビルド用評価関数のところにあるものが使えます。
- やねうら王2018 Otafuku用 KPPT型　→ やねうら王2017Early(KPPT)と同様
- やねうら王2018 Otafuku用 KPP_KKPT型　→ やねうら王2017Early(KPP_KKPT)と同様

### 「Re : ゼロから始める評価関数生活」プロジェクト(略して「リゼロ」)

ゼロベクトルの評価関数(≒駒得のみの評価関数)から、「elmo絞り」(elmo(WCSC27)の手法)を用いて強化学習しました。従来のソフトにはない、不思議な囲いと終盤力が特徴です。
やねうら王2017Earlyの評価関数ファイルと差し替えて使うことが出来ます。フォルダ名に書いてあるepochの数字が大きいものほど新しい世代(強い)です。

- [リゼロ評価関数 epoch 0](https://drive.google.com/open?id=0Bzbi5rbfN85Nb3o1Zkd6cjVNYkE) : 全パラメーターがゼロの初期状態の評価関数です。
- [リゼロ評価関数 epoch 0.1](https://drive.google.com/open?id=0Bzbi5rbfN85NNTBERmhiMGZlSWs) : [解説記事](http://yaneuraou.yaneu.com/2017/06/20/%E5%BE%93%E6%9D%A5%E6%89%8B%E6%B3%95%E3%81%AB%E5%9F%BA%E3%81%A5%E3%81%8F%E3%83%97%E3%83%AD%E3%81%AE%E6%A3%8B%E8%AD%9C%E3%82%92%E7%94%A8%E3%81%84%E3%81%AA%E3%81%84%E8%A9%95%E4%BE%A1%E9%96%A2%E6%95%B0/)
- [リゼロ評価関数 epoch 1から4まで](https://drive.google.com/open?id=0Bzbi5rbfN85NNWY0RTJlc2x5czg) : [解説記事](http://yaneuraou.yaneu.com/2017/06/12/%E4%BA%BA%E9%96%93%E3%81%AE%E6%A3%8B%E8%AD%9C%E3%82%92%E7%94%A8%E3%81%84%E3%81%9A%E3%81%AB%E8%A9%95%E4%BE%A1%E9%96%A2%E6%95%B0%E3%81%AE%E5%AD%A6%E7%BF%92%E3%81%AB%E6%88%90%E5%8A%9F/)
- [リゼロ評価関数 epoch 5から6まで](https://drive.google.com/open?id=0Bzbi5rbfN85NSS0wWkEwSERZVzQ) : [解説記事](http://yaneuraou.yaneu.com/2017/06/13/%E7%B6%9A-%E4%BA%BA%E9%96%93%E3%81%AE%E6%A3%8B%E8%AD%9C%E3%82%92%E7%94%A8%E3%81%84%E3%81%9A%E3%81%AB%E8%A9%95%E4%BE%A1%E9%96%A2%E6%95%B0%E3%81%AE%E5%AD%A6%E7%BF%92/)
- [リゼロ評価関数 epoch 7](https://drive.google.com/open?id=0Bzbi5rbfN85NWWloTFdMRjI5LWs) : [解説記事](http://yaneuraou.yaneu.com/2017/06/15/%E7%B6%9A2-%E4%BA%BA%E9%96%93%E3%81%AE%E6%A3%8B%E8%AD%9C%E3%82%92%E7%94%A8%E3%81%84%E3%81%9A%E3%81%AB%E8%A9%95%E4%BE%A1%E9%96%A2%E6%95%B0%E3%81%AE%E5%AD%A6%E7%BF%92/)
- [リゼロ評価関数 epoch 8](https://drive.google.com/open?id=0Bzbi5rbfN85NMHd0OEUxcUVJQW8) : [解説記事](http://yaneuraou.yaneu.com/2017/06/21/%E3%83%AA%E3%82%BC%E3%83%AD%E8%A9%95%E4%BE%A1%E9%96%A2%E6%95%B0epoch8%E5%85%AC%E9%96%8B%E3%81%97%E3%81%BE%E3%81%97%E3%81%9F%E3%80%82/)

### やねうら王 KPP_KKPT型ビルド用評価関数

やねうら王2017 KPP_KKPT型ビルドで使える評価関数です。

- [リゼロ評価関数 KPP_KKPT型 epoch4](https://drive.google.com/open?id=0Bzbi5rbfN85NSk1qQ042U0RnUEU) : [解説記事](http://yaneuraou.yaneu.com/2017/09/02/%E3%82%84%E3%81%AD%E3%81%86%E3%82%89%E7%8E%8B%E3%80%81kpp_kkpt%E5%9E%8B%E8%A9%95%E4%BE%A1%E9%96%A2%E6%95%B0%E3%81%AB%E5%AF%BE%E5%BF%9C%E3%81%97%E3%81%BE%E3%81%97%E3%81%9F/)

### Shivoray(シボレー) 全自動雑巾絞り機

自分で自分好みの評価関数を作って遊んでみたいという人のために『Shivoray』(シボレー)という全自動雑巾絞り機を公開しました。

- [ShivorayV4.71](https://drive.google.com/open?id=0Bzbi5rbfN85Nb292azZxRmU0R1U) : [解説記事](http://yaneuraou.yaneu.com/2017/06/26/%E3%80%8Eshivoray%E3%80%8F%E5%85%A8%E8%87%AA%E5%8B%95%E9%9B%91%E5%B7%BE%E7%B5%9E%E3%82%8A%E6%A9%9F%E5%85%AC%E9%96%8B%E3%81%97%E3%81%BE%E3%81%97%E3%81%9F/)

## 定跡集

やねうら王2017Earlyで使える、各種定跡集。
ダウンロードしたあと、zipファイルになっているのでそれを解凍して、やねうら王の実行ファイルを配置しているフォルダ配下のbookフォルダに放り込んでください。

- コンセプトおよび定跡フォーマットについて : [やねうら大定跡はじめました](http://yaneuraou.yaneu.com/2016/07/10/%E3%82%84%E3%81%AD%E3%81%86%E3%82%89%E5%A4%A7%E5%AE%9A%E8%B7%A1%E3%81%AF%E3%81%98%E3%82%81%E3%81%BE%E3%81%97%E3%81%9F/)
- 定跡ファイルのダウンロードは[こちら](https://github.com/yaneurao/YaneuraOu/releases/tag/v4.73_book)

## 世界コンピュータ将棋選手権および2017年に開催される第5回将棋電王トーナメントに参加される開発者の方へ

やねうら王をライブラリとして用いて参加される場合、このやねうら王のGitHub上にあるすべてのファイルおよび、このトップページから直リンしているファイルすべてが使えます。

## ライセンス

やねうら王プロジェクトのソースコードはStockfishをそのまま用いている部分が多々あり、Apery/SilentMajorityを参考にしている部分もありますので、やねうら王プロジェクトは、それらのプロジェクトのライセンス(GPLv3)に従うものとします。

「リゼロ評価関数ファイル」については、やねうら王プロジェクトのオリジナルですが、一切の権利は主張しませんのでご自由にお使いください。
