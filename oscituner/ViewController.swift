//
//  ViewController.swift
//  oscituner
//
//  Created by Denis Kreshikhin on 11.12.14.
//  Copyright (c) 2014 Denis Kreshikhin. All rights reserved.
//

import UIKit

class ViewController: UIViewController {
    var wavePoints: [Double] = [Double](count: 512, repeatedValue: 0)
    var spectrumPoints: [Double] = [Double](count: 512, repeatedValue: 0)
    
    override func viewDidLoad() {
        super.viewDidLoad()
        // Do any additional setup after loading the view, typically from a nib.
        
        let f = self.view.frame
        NSLog("%@ %@ %@ %@", f.origin.x, f.origin.y, f.size.width, f.size.height)
        
        var source = Source()
        // var source = MicSource()
        var processing = Processing()
        
        let tubeFrame = getOptimalTubeFrame(self.view.frame.size)
        var tube = TubeView(frame: tubeFrame)
        
        source.OnData = { (sample: [Double]) -> () in
            processing.Push(sample)
            
            var spectrum = processing.BuildSpectrumForFrequency()
            var wave = processing.BuidStandingWaveForFrequency(200)
            
            var i = 0
            for w in wave {
                self.wavePoints[i] = (Double(i) / Double(self.wavePoints.count) - 0.5) * 1.9
                self.wavePoints[i+1] = Double(w) / 20.0 - 0.4
                i += 2
            }
            
            i = 0
            for s in spectrum {
                self.spectrumPoints[i] = (Double(i) / Double(self.spectrumPoints.count) - 0.5) * 1.9
                self.spectrumPoints[i+1] = Double(s) / 2.0 + 0.4
                i += 2
            }
        }
        
        
        self.view.addSubview(tube)
        
        let panelFrame = getOptimalPanelFrame(self.view.frame.size)
        self.view.addSubview(PanelView(frame: panelFrame))
        
        self.view.backgroundColor = UIColor.redColor()
        
        
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func getOptimalTubeFrame(size: CGSize) -> CGRect {
        var height: CGFloat = 100.0
        
        if size.width > 568 { // 6
            height = 117.2
        }
        
        if size.width > 667 { //6s
            height = 177.5
        }
        
        return CGRectMake(0, 0, size.width, size.height - height)
    }
    
    func getOptimalPanelFrame(size: CGSize) -> CGRect {
        var height: CGFloat = 100.0
        
        if size.width > 568 { // 6
            height = 117.2
        }
        
        if size.width > 667 { //6s
            height = 177.5
        }
        
        return CGRectMake(0, size.height - height, size.width, height)
    }
}